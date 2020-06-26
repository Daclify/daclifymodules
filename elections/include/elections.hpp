#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include <external_structs.hpp>

using namespace std;
using namespace eosio;

CONTRACT elections : public contract {
  public:
    using contract::contract;
    /*
    {
      "cand_registration": true,
      "voting": true,
      "elections": true,
      "electorate": false,
      "max_pay": {"quantity":"0","contract":""},
      "cand_min_stake": {"quantity":"1.0000 EOS","contract":"eosio.token"},
      "max_votes": 5,
      "force_max_votes": false,
      "allow_self_vote": false,
      "election_period_sec": 120,
      "cand_stake_release_sec": 360,
      "stake_release_sec":  60,
      "parent": "piecestest55",
      "weight_provider": ""
    }
    */
    struct mod_config{

      bool cand_registration = false;
      bool voting = false;
      bool elections = false;
      bool electorate = false;
      extended_asset max_pay;
      extended_asset cand_min_stake;
      uint8_t max_votes = 5;
      bool force_max_votes = false;
      bool allow_self_vote = false;
      uint64_t election_period_sec = 60*60*24*14;
      uint64_t cand_stake_release_sec = 60*60*24*14;
      uint64_t stake_release_sec = 60*60*24*7;
      name parent;
      name weight_provider;
    
    };


    ACTION updateconf(mod_config new_conf, bool remove);
    ACTION newelection(name actor);
    ACTION regcand(name account);
    ACTION pausecampaig(name candidate, bool is_active);
    ACTION unregcand(name candidate);
    ACTION updatepay(name candidate, extended_asset new_pay);
    ACTION vote(name voter, vector<name> new_votes);

    ACTION iweightupdat(name provider, name account, uint64_t new_weight);

    ACTION addelectorat(vector<name> voters);
    ACTION remelectorat(vector<name> voters);

    ACTION clearcands();
    ACTION clearstate();
    ACTION clearvoters();


    ACTION openstake(name member, extended_asset stakeasset);
    ACTION unstake(name member, extended_asset stakeasset);
    ACTION claimstake(name member, uint64_t id);

    //notification handlers
    [[eosio::on_notify("*::transfer")]]
    void on_transfer(name from, name to, asset quantity, string memo);

  private:
    //
    TABLE config{
      mod_config conf;
    };
    typedef eosio::singleton<"config"_n, config> config_table;


    TABLE state {
      uint64_t election_count=0;
      uint64_t active_candidate_count=0;
      uint64_t inactive_candidate_count=0;
      uint64_t total_vote_weight=0;
      time_point_sec last_election = time_point_sec(0);
    };
    typedef eosio::singleton<"state"_n, state> state_table;


    TABLE voters{
      name    voter;
      vector<name>  votes;
      time_point_sec last_voted;
      uint64_t last_vote_weight;
      auto primary_key() const { return voter.value; }
    };
    typedef multi_index<name("voters"), voters> voters_table;

    //
    TABLE candidates{
      name cand;
      uint64_t total_votes = 0;
      bool is_active = 0;
      time_point_sec registered;
      extended_asset pay;

      auto primary_key() const { return cand.value; }
      uint64_t by_votes() const { return static_cast<uint64_t>(UINT64_MAX - total_votes);}
    };
    typedef multi_index<name("candidates"), candidates,
      eosio::indexed_by<"byvotes"_n, eosio::const_mem_fun<candidates, uint64_t, &candidates::by_votes >>
    > candidates_table;

    // selected voters, exclude others
    //scoped: whitelist & blacklist
    TABLE electorate{
      name    voter;
      auto primary_key() const { return voter.value; }
    };
    typedef multi_index<name("electorate"), electorate> electorate_table;

    //scoped table
    TABLE stake {
      extended_asset balance;
      time_point_sec last_staked;
      uint64_t primary_key()const { return balance.quantity.symbol.raw(); }
    };
    typedef multi_index<"stake"_n, stake> stake_table;

    TABLE stakerelease {
      uint64_t id;
      name member;
      extended_asset balance;
      time_point_sec release_time;
      uint64_t primary_key()const { return id; }
      uint64_t by_member() const { return member.value;}
    };
    typedef multi_index<name("stakerelease"), stakerelease,
      eosio::indexed_by<"bymember"_n, eosio::const_mem_fun<stakerelease, uint64_t, &stakerelease::by_member >>
    > stakerelease_table;



    //functions//
    bool is_candidate(const name& cand);
    void propagate_votes(vector<name> old_votes, vector<name> new_votes, uint64_t old_vote_weight, uint64_t new_vote_weight);
    mod_config get_config();
    void add_stake( const name& account, const extended_asset& value);
    void sub_stake( const name& account, const extended_asset& value);
};
