ACTION hooks::hookreg(name hooked_action, name hooked_contract, name hook_action_name, string description){
  require_auth(get_self());

  actionhooks_table _actionhooks(get_self(), get_self().value);
  auto by_hook = _actionhooks.get_index<"byhook"_n>();
  uint128_t composite_id = (uint128_t{hooked_action.value} << 64) | hooked_contract.value;
  auto hook_itr = by_hook.find(composite_id);

  check(hook_itr == by_hook.end(), "Action "+hooked_contract.to_string()+"::"+hooked_action.to_string()+" already has a hook.");

  _actionhooks.emplace( get_self(), [&]( auto& n){
      n.hook_id = _actionhooks.available_primary_key();
      n.hooked_action = hooked_action;
      n.hooked_contract = hooked_contract;
      n.hook_action_name = hook_action_name;
      n.description = description;
      n.enabled = false;
  });

}

ACTION hooks::hookdel(uint64_t hook_id){
  require_auth(get_self());
  actionhooks_table _actionhooks(get_self(), get_self().value);
  auto hook_itr = _actionhooks.find(hook_id);
  check(hook_itr != _actionhooks.end(), "Hook with this id doesn't exist");
  _actionhooks.erase(hook_itr);
}

ACTION hooks::hookenable(uint64_t hook_id, bool enable){
  require_auth(get_self());
  actionhooks_table _actionhooks(get_self(), get_self().value);
  auto hook_itr = _actionhooks.find(hook_id);
  check(hook_itr != _actionhooks.end(), "Hook with this id doesn't exist");
  check(hook_itr->enabled != enable, "Hook already enabled="+to_string(enable) );

  _actionhooks.modify( hook_itr, same_payer, [&]( auto& n) {
    n.enabled = enable;
  });   
}