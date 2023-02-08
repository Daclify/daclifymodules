#include <elections.hpp>
#include <functions.cpp>


/**
 * updateconf allows the contract owner to update the configuration
 * 
 * @param new_conf The new configuration to be set.
 * @param remove If true, the configuration will be removed.
 * 
 */
ACTION elections::updateconf(mod_config new_conf, bool remove){
    require_auth(get_self());

    config_table _config(get_self(), get_self().value);
    if(remove){
      _config.remove();
      return;
    }

    new_conf.weight_provider = new_conf.weight_provider == name(0) ? get_self() : new_conf.weight_provider;
    check(is_account(new_conf.weight_provider), "weight provider is not an existing account");

    auto conf = _config.get_or_default(config());
    conf.conf = new_conf;
    _config.set(conf, get_self());
}

/**
 * newelection action sets up a new election.
 * The action checks if the elections are enabled, if there are active candidates, if the time since the last
 * election is greater than the election period, and if the maximum number of custodians is greater
 * than zero. If all of these conditions are met, it selects the top candidates by total_votes and sets
 * them as the new custodians. It also pays the candidates if the maximum payment is greater than zero
 * 
 * @param actor the account that is calling the action
 */
ACTION elections::newelection(name actor){

  require_auth(actor);
  mod_config conf = get_config();
  check(conf.elections, "Elections are disabled");

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );
  time_point_sec now = time_point_sec(current_time_point());
  time_point_sec new_election_time = time_point_sec(conf.election_period_sec + s.last_election.sec_since_epoch() );
  check(now >= new_election_time, "New election too early");

  check(s.active_candidate_count > 0, "There are no active candidates");


  coreconf_table _coreconf(conf.parent, conf.parent.value);
  auto coresettings = _coreconf.get();
  int max_cust = coresettings.conf.max_custodians;
  check(max_cust > 0, "Maximum custodians of the group can't be set to zero during elections");


  candidates_table _candidates(get_self(), get_self().value);
  auto by_votes = _candidates.get_index<"byvotes"_n>();
  
  auto cand_itr = by_votes.begin();


  bool table_end_flag = false;
  vector<name> new_custs;
  struct payment{
    name receiver;
    asset amount;
  };
  vector<payment> payments;


  while(new_custs.size() < max_cust && !table_end_flag){
    //ignore inactive candidates and zero total_votes
    if(cand_itr != by_votes.end() && cand_itr->total_votes > 0 ){
      if(cand_itr->state == ACTIVE){
        new_custs.push_back(cand_itr->cand);
        if(cand_itr->pay.quantity.amount > 0 && conf.max_pay.quantity.amount > 0){
          payments.push_back(payment{cand_itr->cand, cand_itr->pay.quantity} );
        }
        
      }
      cand_itr++;
    }
    else{
      table_end_flag = true;
    }
  }

  check(new_custs.size() > 0, "There are no new active custodians with total_votes > 0.");

  //debug
  /*
  for(name c : new_custs){
    print(c.to_string()+", ");
  }
  */

  s.election_count += 1;
  s.last_election = now;
  _state.set(s, get_self() );

  action(
    permission_level{get_self(), "active"_n},
    conf.parent, "isetcusts"_n,
    std::make_tuple(new_custs)
  ).send();

  if(conf.max_pay.quantity.amount && payments.size() ){
    string memo = "election round #"+to_string(s.election_count);
    action(
      permission_level{ get_self(), "active"_n },
      conf.parent,
      "ipayroll"_n,
      std::make_tuple(
        name("elections"),//sender_module_name
        name("custodians"),//payroll_tag
        payments,
        memo,
        time_point_sec(conf.election_period_sec + now.sec_since_epoch() ),//due_date,
        uint8_t(1),//repeat,
        uint64_t(0),//recurrence_sec,
        1//auto_pay (bool)
      )
    ).send(); 
  
  }

}

/**
 * The regcand action allows a member of the group to register as a candidate
 * 
 * @param account The account that is registering as a candidate.
 */
ACTION elections::regcand(name account){

  require_auth(account);
  
  mod_config conf = get_config();
  check(conf.cand_registration, "Candidate registration is disabled.");
  check(is_member(conf.parent, account), "Account is not a member of the group.");

  //only allow stakers
  if(conf.cand_min_stake.quantity.amount > 0 ){
    //check if candidate staked enough else assert
    stake_table _stake( get_self(), account.value);
    auto s_itr = _stake.find( conf.cand_min_stake.quantity.symbol.raw() );
    check(s_itr != _stake.end(), "Not staked for candidancy" );
    check(s_itr->balance.quantity.amount >= conf.cand_min_stake.quantity.amount, "Not enough staked for candidancy.");
  }

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );

  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(account.value);

  //not in table yet
  if(cand_itr == _candidates.end() ){
    _candidates.emplace(account, [&](auto& n) {
      n.cand = account;
      n.registered = time_point_sec(current_time_point());
      n.state = ACTIVE; //1
      n.total_votes=0;
    });
    s.active_candidate_count++;
  }
  else{
    check(cand_itr->state != FIRED, "Fired account can't register as candidate again. Contact custodians.");
    check(cand_itr->state != ACTIVE && cand_itr->state != PAUSED, "Account is already a registered candidate.");

    _candidates.modify(cand_itr, same_payer, [&](auto& n) {
      n.state = ACTIVE;
      n.registered = time_point_sec(current_time_point());
    });

    s.active_candidate_count++;
    s.unregistered_candidate_count--;
  
  }

  _state.set(s, get_self() );

}

/**
 * `pausecampaig` action allows a candidate to pause or resume their campaign
 * 
 * @param candidate The account name of the candidate.
 * @param paused true if you want to pause the campaign, false if you want to resume it.
 */
ACTION elections::pausecampaig(name candidate, bool paused){
  require_auth(candidate);
  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(candidate.value);
  check(cand_itr != _candidates.end(), "Candidate not found.");
  
  check(cand_itr->state != FIRED, "Fired accounts can't pause campaign. Contact custodians.");
  check(cand_itr->state != UNREGISTERED, "Register as candidate first.");

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );  

  if(paused){
    check(cand_itr->state == ACTIVE, "Campaign already in paused state.");
    s.active_candidate_count--;
    s.paused_candidate_count++;
  }
  else{
    check(cand_itr->state == PAUSED, "Campaign already in active state.");
    s.active_candidate_count++;
    s.paused_candidate_count--;
  }
  _candidates.modify(cand_itr, same_payer, [&](auto& n) {
    n.state = paused ? PAUSED : ACTIVE;
  });
  _state.set(s, get_self() );
}

/**
 * The action `unregcand` is used to unregister a candidate
 * 
 * @param candidate The account name of the candidate to unregister.
 */
ACTION elections::unregcand(name candidate){
  require_auth(candidate);
  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(candidate.value);
  check(cand_itr != _candidates.end(), "Candidate not found.");
  check(cand_itr->state != FIRED, "Candidate fired.");
  check(cand_itr->state != UNREGISTERED, "Candidate already unregistered.");
  
  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );

  if(cand_itr->state == ACTIVE){
    s.active_candidate_count--;
    s.unregistered_candidate_count++;
  }
  else if(cand_itr->state == PAUSED){
    s.paused_candidate_count--;
    s.unregistered_candidate_count++;
  }
  _state.set(s, get_self() );

  _candidates.modify(cand_itr, same_payer, [&](auto& n) {
    n.state = UNREGISTERED;
  });
}

/**
 * The 'firecand' action fires a candidate
 * 
 * @param account The account name of the candidate to fire.
 */
ACTION elections::firecand(name account){
  require_auth(get_self() );
  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(account.value);
  check(cand_itr != _candidates.end(), "Candidate not found.");

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );

  check(cand_itr->state != FIRED, "candidate already fired");

  if(cand_itr->state == UNREGISTERED){
    s.unregistered_candidate_count--;
  }
  else if(cand_itr->state == PAUSED){
    s.paused_candidate_count--;
  }
  else if(cand_itr->state == ACTIVE){
    s.active_candidate_count--;
  }
  s.fired_candidate_count++;
  _state.set(s, get_self() );

  _candidates.modify(cand_itr, same_payer, [&](auto& n) {
    n.state = FIRED;
  });
}

/**
 * The 'unfirecand' action takes a candidate account name as an argument, checks that the candidate is fired, and then
 * changes the candidate's state to unregistered
 * 
 * @param account the account name of the candidate to unfire
 */
ACTION elections::unfirecand(name account){
  require_auth(get_self() );
  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(account.value);
  check(cand_itr != _candidates.end(), "Candidate not found.");
  check(cand_itr->state == FIRED, "candidate not fired");

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );

  s.unregistered_candidate_count++;
  s.fired_candidate_count--;

  _state.set(s, get_self() );

  _candidates.modify(cand_itr, same_payer, [&](auto& n) {
    n.state = UNREGISTERED;
  });
}

/**
 * The 'updatepay' action allows a candidate to update their payment request
 * 
 * @param candidate The account name of the candidate.
 * @param new_pay The new pay for the candidate.
 */
ACTION elections::updatepay(name candidate, extended_asset new_pay){
  require_auth(candidate);
  candidates_table _candidates(get_self(), get_self().value);
  auto cand_itr = _candidates.find(candidate.value);
  check(cand_itr != _candidates.end(), "Candidate not found.");
  mod_config conf = get_config();
  check(conf.max_pay.quantity.amount > 0, "Payments not enabled.");
  check(new_pay <= conf.max_pay, "Invalid pay.");
  check(cand_itr->state == ACTIVE, "Candidate must be active to update pay.");

  _candidates.modify(cand_itr, same_payer, [&](auto& n) {
    n.pay = new_pay;
  });
}

/**
 * The 'vote' action takes a voter and a vector of votes and updates the voters table with those new votes
 * 
 * @param voter The account that is voting.
 * @param new_votes a vector of names of the candidates you want to vote for.
 */
ACTION elections::vote(name voter, vector<name> new_votes){

  //check if voter is eligible to vote
  require_auth(voter);
  mod_config conf = get_config();
  check(conf.voting, "Voting is disabled");
  check(is_member(conf.parent, voter), "Voter is not a member of the group.");
  check(new_votes.size() <= conf.max_votes, "Too many votes.");
  if(conf.force_max_votes && new_votes.size() > 0){
    check(conf.force_max_votes == new_votes.size(), "Obligated max votes not met." );
  }
  //check if electorate is enabled
  if(conf.electorate){
    electorate_table _electorate(get_self(), get_self().value);
    auto v_itr = _electorate.find(voter.value);
    check(v_itr != _electorate.end(), "Account not included in electorate." );
  }

  voters_table _voters(get_self(), get_self().value);
  auto voter_itr = _voters.find(voter.value);

  candidates_table _candidates(get_self(), get_self().value);
  //deduplicate and validate votes
  std::set<name> dupvotes;

  for (name vote: new_votes) {
    check(dupvotes.insert(vote).second, "Duplicate candidate vote.");
    auto cand = _candidates.get(vote.value, "Vote for non existing candidate ");

    check(cand.state == ACTIVE, "Vote for inactive candidate.");
    if(!conf.allow_self_vote){
      check(voter != vote, "Voting for self not allowed");
    }
  }


  bool is_new_voter = voter_itr == _voters.end();

  vector<name> old_votes = is_new_voter ? old_votes : voter_itr->votes;//set votes to empty vector if is new voter else use old votes
  uint64_t old_vote_weight = is_new_voter ? 0 : voter_itr->last_vote_weight;

  //get new voteweight of voter
  uint64_t new_vote_weight;//todo get it
  if(conf.weight_provider != get_self() ) {
    new_vote_weight = get_external_weight(conf.weight_provider, voter);
  }
  else{
    new_vote_weight = 1;
  }
  //void elections::propagate_votes(vector<name> old_votes, vector<name> new_votes, uint64_t old_vote_weight, uint64_t new_vote_weight)
  propagate_votes(old_votes, new_votes, old_vote_weight, new_vote_weight);

  //update/insert voters
  time_point_sec now = time_point_sec(current_time_point());
  if(is_new_voter){
    _voters.emplace(voter, [&](auto& n) {
      n.voter = voter;
      n.votes = new_votes;
      n.last_voted = now;
      n.last_vote_weight= new_vote_weight;
    });

  }
  else{
    _voters.modify(voter_itr, same_payer, [&](auto& n) {
      n.votes = new_votes;
      n.last_voted = now;
      n.last_vote_weight = new_vote_weight;
    });

  }
  
}

/**
 * The 'addelectorat' action adds voters to the list of voters
 * 
 * @param voters a vector of account names that will be added to the electorate.
 */
ACTION elections::addelectorat(vector<name> voters){
  require_auth(get_self() );
  electorate_table _electorate(get_self(), get_self().value);

  mod_config conf = get_config();
  for (name voter: voters) {
    check(is_member(conf.parent, voter), "Account is not a member of the group.");
    auto existing_voter = _electorate.find(voter.value);
    if (existing_voter == _electorate.end()){
      //new voter
      _electorate.emplace(get_self(), [&](auto& n) {
          n.voter = voter;
      });
    }
  }
}

/**
 * The 'remelectorat' action removes voters from the electorate table.
 * 
 * @param voters a vector of account names that will be removed from the electorate
 */
ACTION elections::remelectorat(vector<name> voters){
  require_auth(get_self() );
  electorate_table _electorate(get_self(), get_self().value);

  for (name voter: voters) {
    auto existing_voter = _electorate.find(voter.value);
    if (existing_voter != _electorate.end()){
      //new voter
      _electorate.erase(existing_voter);
    }
  }
}

/**
 * The `openstake` action allows a member to open a stake for a specific asset
 * 
 * @param member The account that is opening the stake
 * @param stakeasset The asset that is being staked.
 */
ACTION elections::openstake(name member, extended_asset stakeasset){
  require_auth(member);
  mod_config conf = get_config();
  check(is_member(conf.parent, member), "Account is not a member of the group.");
  
  stake_table _stake( get_self(), member.value);
  auto itr = _stake.find( stakeasset.quantity.symbol.raw() );

  if( itr == _stake.end() ) {
    stakeasset.quantity.amount = 0;
    _stake.emplace( member, [&]( auto& a){
      a.balance = stakeasset;
    });
  } 
}

/**
 * The 'unstake' action allows a member to unstake an amount of a specified token
 * 
 * @param member The account name of the member who is unstaking.
 * @param stakeasset The asset to be unstaked.
 */
ACTION elections::unstake(name member, extended_asset stakeasset){
  require_auth(member);
  mod_config conf = get_config();
  check(is_member(conf.parent, member), "Account is not a member of the group.");
  check(stakeasset.quantity.amount > 0, "Amount must be greater then zero.");

  time_point_sec release;
  if(stakeasset.contract == conf.cand_min_stake.contract && stakeasset.quantity.symbol == conf.cand_min_stake.quantity.symbol){
    //check(!is_candidate(member), "Unregister as candidate first.");
    candidates_table _candidates(get_self(), get_self().value);
    auto cand_itr = _candidates.find(member.value);
    check(cand_itr != _candidates.end(), "member not in the candidate table.");
    check(cand_itr->state == UNREGISTERED, "Unregister as candidate first.");
    release = time_point_sec(current_time_point().sec_since_epoch() + conf.cand_stake_release_sec);
  }
  else{
    release = time_point_sec(current_time_point().sec_since_epoch() + conf.stake_release_sec);
  }

  sub_stake(member, stakeasset);

  stakerelease_table _stakerelease(get_self(), get_self().value );
  _stakerelease.emplace(member, [&](auto& n) {
    n.id = _stakerelease.available_primary_key();
    n.member = member;
    n.balance = stakeasset;
    n.release_time = release;
  });
}

/**
 * The 'claimstake' action allows a member to claim their stake back after the release time has been met
 * 
 * @param member The account name of the member who is claiming the stake.
 * @param id The id of the stake release.
 */
ACTION elections::claimstake(name member, uint64_t id){
  require_auth(member);
  stakerelease_table _stakerelease(get_self(), get_self().value );
  auto itr = _stakerelease.find(id);
  check(itr != _stakerelease.end(), "No stake waiting to be release with this id.");
  check(itr->member == member, "Account is not the owner of this stake release");
  time_point_sec now = time_point_sec(current_time_point());
  check(now > itr->release_time,"Release time not met.");

  action(
    permission_level{get_self(), "active"_n},
    itr->balance.contract, "transfer"_n,
    make_tuple(get_self(), member, itr->balance.quantity, string("Refund stake."))
  ).send();

  _stakerelease.erase(itr);
}

/**
 * The `iweightupdat` action is called by the weight provider to update the weight of a voter
 * 
 * @param provider The account that is allowed to push weight updates.
 * @param account The account that is voting
 * @param new_weight the new weight of the voter
 */
ACTION elections::iweightupdat(name provider, name account, uint64_t new_weight){
  require_auth(provider);
  mod_config conf = get_config();
  check(provider == conf.weight_provider, "Account not allowed to push weight updates.");

  voters_table _voters(get_self(), get_self().value);
  auto voter_itr = _voters.find(account.value);

  if(voter_itr == _voters.end() ){
    //provider pays for ram, will be released when voter actually votes
    _voters.emplace(provider, [&](auto& n) {
      n.voter = account;
      n.votes = {};
      n.last_voted = time_point_sec(0);
      n.last_vote_weight= new_weight;
    });
  }
  else{
    //weights need to be propagated
    
    propagate_votes(voter_itr->votes, voter_itr->votes, voter_itr->last_vote_weight, new_weight);

    _voters.modify(voter_itr, same_payer, [&](auto& n) {
      n.last_vote_weight = new_weight;
    });
  
  }
  

}

//notify transfer handler

/**
 * If the transfer is to the elections contract, and the memo starts with "candidate stake", then the
 * transfer is added to the candidate's stake
 * 
 * @param from The account that sent the funds.
 * @param to The account that is receiving the funds.
 * @param quantity The amount of tokens being transferred.
 * @param memo The memo field of the transfer action.
 * 
 * @return Nothing.
 */
void elections::on_transfer(name from, name to, asset quantity, string memo){
  if(from == to){
    return;
  }
  mod_config conf = get_config();

  if(to == get_self() ){

    if(memo.substr(0, 15) == "candidate stake" ){
      check(quantity.amount > 0, "Stake amount must be greater then zero.");
      check(is_member(conf.parent, from), "Account is not a member of the group.");
      check(conf.cand_min_stake.quantity.amount > 0, "Candidate stake is disabled.");
      check(conf.cand_min_stake.quantity.symbol == quantity.symbol, "Symbol not allowed as candidate stake.");
      check(conf.cand_min_stake.contract == get_first_receiver(), "Attempt to stake from malicious contract?");
      add_stake(from, extended_asset(quantity, get_first_receiver()) );
    }

  }

}

//def

/**
 * The 'clearcands' action clears the candidates table
 */
ACTION elections::clearcands(){
    require_auth(get_self());
    candidates_table _candidates(get_self(), get_self().value);
    auto itr = _candidates.begin();
    while(itr != _candidates.end() ) {
        itr = _candidates.erase(itr);
    }
}

/**
 * The 'clearstate' action clears the state table
 */
ACTION elections::clearstate(){
    require_auth(get_self());
    state_table _state(get_self(), get_self().value);
    _state.remove();
}

/**
 * The 'clearvoters' action clears the voters table.
 */
ACTION elections::clearvoters(){
    require_auth(get_self());
    voters_table _voters(get_self(), get_self().value);
    auto itr = _voters.begin();
    while(itr != _voters.end() ) {
        itr = _voters.erase(itr);
    }
}


