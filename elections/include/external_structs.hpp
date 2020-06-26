struct members {
  eosio::name account;
  eosio::time_point_sec agreement_date;
  uint64_t r1;
  uint64_t r2;
  auto primary_key() const { return account.value; }
};
typedef eosio::multi_index<eosio::name("members"), members> members_table;


bool is_member(eosio::name group, eosio::name account){
  members_table _members(group, group.value);
  auto itr = _members.find(account.value);
  if(itr == _members.end() ){
    return false;
  }
  else{
    return true;
  }
}

struct voteweight {
  eosio::name account;
  uint64_t weight;
  auto primary_key() const { return account.value; }
};
typedef eosio::multi_index<eosio::name("voteweight"), voteweight> voteweight_table;

uint64_t get_external_weight(eosio::name provider, eosio::name account){
  voteweight_table _voteweight(provider, provider.value);
  auto itr = _voteweight.find(account.value);
  if(itr == _voteweight.end() ){
    return 0;
  }
  else{
    return itr->weight;
  }
}


struct groupconf{
  uint8_t max_custodians ;
  uint32_t inactivate_cust_after_sec;
  bool exec_on_threshold_zero;
  uint8_t proposal_archive_size;
  bool member_registration;
  bool withdrawals;
  bool internal_transfers;
  bool deposits;
  eosio::name maintainer_account;
      //user_agreement user_agreement;
};
struct coreconf{
  groupconf conf;
};
typedef eosio::singleton<"coreconf"_n, coreconf> coreconf_table;