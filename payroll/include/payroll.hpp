#include <eosio/eosio.hpp>
#include <eosio/permission.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>

using namespace std;
using namespace eosio;

CONTRACT payroll : public contract {
  public:
    using contract::contract;

    ACTION payrollreg(name payroll_tag, name pay_from, name xfer_permission, extended_asset payment_token, string description);
    ACTION payrollrem(name payroll_tag);
    ACTION payrolldesc(name payroll_tag, string description);
    ACTION paymentadd(name payroll_tag, name receiver, asset amount, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay);
    ACTION paymentrem(uint64_t pay_id);
    ACTION pay(uint64_t pay_id);


    ACTION freeze(bool freeze);


    TABLE state {
      bool freeze_payments = true;
      uint64_t next_pay_id = 1;
    };
    typedef eosio::singleton<"state"_n, state> state_table;

  private:
    TABLE payrolls {
      name payroll_tag;
      permission_level pay_permission;
      extended_asset total_paid;
      asset total_allocated;
      string description;
      auto primary_key() const { return payroll_tag.value; }
    };
    typedef multi_index<name("payrolls"), payrolls> payrolls_table;


    TABLE payments {
      uint64_t pay_id;
      name payroll_tag;
      name receiver;
      asset amount;
      time_point_sec submitted = time_point_sec(current_time_point().sec_since_epoch());
      time_point_sec due_date;
      uint8_t repeat=1;
      uint8_t repeated=0;
      uint64_t recurrence_sec=0;
      bool auto_pay = 1;

      auto primary_key() const { return pay_id; }
      uint64_t by_receiver() const { return receiver.value; }
      uint64_t by_payroll() const { return payroll_tag.value; }
    };
    typedef multi_index<name("payments"), payments,
      eosio::indexed_by<"byreceiver"_n, eosio::const_mem_fun<payments, uint64_t, &payments::by_receiver>>,
      eosio::indexed_by<"bypayroll"_n, eosio::const_mem_fun<payments, uint64_t, &payments::by_payroll>>
    > payments_table;









    bool is_master_authorized_to_use_slave(const permission_level& master, const permission_level& slave){
      vector<permission_level> masterperm = { master };
      auto packed_master = pack(masterperm);
      auto test = eosio::check_permission_authorization(
                  slave.actor,
                  slave.permission,
                  (const char *) 0, 0,
                  packed_master.data(),
                  packed_master.size(), 
                  microseconds(0)
      );
      return test > 0 ? true : false;
    };


};
