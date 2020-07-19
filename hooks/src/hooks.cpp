#include <hooks.hpp>
#include <hook_management.cpp>

//add your hooks here

ACTION hooks::dummyhook(name hooked_action){
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //all hooks need to have this on top!!!
  Hook(hooked_action, get_self() );
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  vector<payment> payments;
  payments.push_back(  payment{name("piecesnbits1"), asset(1, symbol(symbol_code("EOS"), 4))}  ) ;

  action(
    permission_level{ get_self(), "active"_n },
    name("111testgroup"),
    "ipayroll"_n,
    std::make_tuple(
      name("hooks"),//sender_module_name
      name("workers"),//payroll_tag
      payments,
      string("payment via propose hook"),
      time_point_sec(current_time_point().sec_since_epoch()+(60*60*10) ),
      uint8_t(1),//repeat,
      uint64_t(0),//recurrence_sec,
      1//auto_pay (bool)
    )
  ).send(); 

}

ACTION hooks::changecolor(name hooked_action){
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  //all hooks need to have this on top!!!
  Hook myhook(hooked_action, get_self() );
  //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  uint64_t n = myhook.gethook().exec_count;

  vector<string>colors = {string("#EB68F2"), string("#798CEF"), string("#F26E67"), string("#900D97") };


  int index = n % colors.size();

  action(
    permission_level{ name("111testgroup"), "hooks"_n },
    name("daclifyhub11"),
    "updatecolor"_n,
    std::make_tuple(
      name("111testgroup"),
      colors[index]
    )
  ).send(); 

}





