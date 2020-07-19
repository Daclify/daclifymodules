#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/asset.hpp>
#include <external_structs.hpp>

//#include <module.hpp>

using namespace std;
using namespace eosio;

CONTRACT hooks : public contract {
  public:
    using contract::contract;

    ACTION hookreg(name hooked_action, name hooked_contract, name hook_action_name, string description);
    ACTION hookdel(uint64_t hook_id);
    ACTION hookenable(uint64_t hook_id, bool enable);

    //custom hooks
    ACTION dummyhook(name hooked_action);
    ACTION changecolor(name hooked_action);


  private:
    TABLE actionhooks {
      uint64_t hook_id;
      name hooked_action;// must be the action name on which to apply the hook
      name hooked_contract;// must be the contract where the hooked_action is
      name hook_action_name;
      string description;
      uint64_t exec_count;
      time_point_sec last_exec;
      bool enabled;
      auto primary_key() const { return hook_id; }
      uint128_t by_hook() const { return (uint128_t{hooked_action.value} << 64) | hooked_contract.value; }

    };
    typedef multi_index<name("actionhooks"), actionhooks,
      eosio::indexed_by<"byhook"_n, eosio::const_mem_fun<actionhooks, uint128_t, &actionhooks::by_hook>>
    > actionhooks_table;



    struct  Hook{
      actionhooks_table _actionhooks;
      //uint64_t hook_id;
      actionhooks hook;

      Hook(eosio::name hooked_action, eosio::name self_): _actionhooks(self_, self_.value)  { 
        eosio::name hooked_contract = eosio::get_sender();
        eosio::check(hooked_contract.value != 0, "Hook can only be triggered by inline action.");
        eosio::require_auth(hooked_contract);

        auto by_hook = _actionhooks.get_index<"byhook"_n>();
        uint128_t composite_id = (uint128_t{hooked_action.value} << 64) | hooked_contract.value;
        auto hook_itr = by_hook.find(composite_id);
        eosio::check(hook_itr != by_hook.end(), "action not hooked");
        eosio::check(hook_itr->enabled, "hook not enabled");
        //hook_id = hook_itr->hook_id;
        hook = *hook_itr;
      }

      ~Hook(){
        _actionhooks.modify( _actionhooks.find(hook.hook_id), same_payer, [&]( auto& n) {
            n.exec_count += 1;
            n.last_exec = eosio::time_point_sec(current_time_point());
        });
      }

      actionhooks gethook(){
        return hook;
      }
    

    };

};
