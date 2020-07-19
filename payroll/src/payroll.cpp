#include <payroll.hpp>

ACTION payroll::payrollreg(name payroll_tag, name pay_from, name xfer_permission, extended_asset payment_token, string description) {
  require_auth(get_self() );
  check(payroll_tag.value, "payroll_tag can't be empty");

  payrolls_table _payrolls(get_self(), get_self().value);
  auto itr = _payrolls.find(payroll_tag.value);
  check(itr == _payrolls.end(), "employer with this tag already registered");

  permission_level slave_perm = permission_level(pay_from, xfer_permission);
  permission_level self_perm = permission_level(get_self(), name("active"));

  check(
      is_master_authorized_to_use_slave(self_perm, slave_perm), 
      "Payroll doesn't have permission to use the employers pay_from account"
  );

  payment_token.quantity.amount = 0;
  _payrolls.emplace(get_self(), [&](auto& n) {
      n.payroll_tag = payroll_tag;
      n.pay_permission = slave_perm;
      n.total_paid = payment_token;
      n.total_allocated = payment_token.quantity;
      n.description = description;
  });
}

ACTION payroll::payrollrem(name payroll_tag) {
  require_auth(get_self() );
  check(payroll_tag.value, "payroll_tag can't be empty");

  payrolls_table _payrolls(get_self(), get_self().value);
  auto itr = _payrolls.find(payroll_tag.value);
  check(itr != _payrolls.end(), "payroll with this tag not registered");

  payments_table _payments(get_self(), get_self().value);
  auto by_payroll = _payments.get_index<"bypayroll"_n>();
  auto existing_payment = by_payroll.find(payroll_tag.value);
  check(existing_payment == by_payroll.end(), "Can't delete a payroll with active payments. please remove payments first.");

  _payrolls.erase(itr);
}

ACTION payroll::payrolldesc(name payroll_tag, string description){
  require_auth(get_self() );
  check(payroll_tag.value, "payroll_tag can't be empty");
  payrolls_table _payrolls(get_self(), get_self().value);
  auto itr = _payrolls.find(payroll_tag.value);
  check(itr != _payrolls.end(), "payroll with this tag not registered");
  _payrolls.modify( itr, same_payer, [&]( auto& n) {
      n.description = description;
  });
}


ACTION payroll::pay(uint64_t pay_id){

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_create(get_self(), state() );
  check(!s.freeze_payments, "Payments are frozen");

  //find the pay_id
  payments_table _payments(get_self(), get_self().value);
  auto payment = _payments.find(pay_id);
  check(payment != _payments.end(), "payment with this id not on the payroll");

  //allow auto pay?
  if(!payment->auto_pay){
    require_auth(payment->receiver);
  }

  //find the associated payroll
  payrolls_table _payrolls(get_self(), get_self().value);
  auto payroll_itr = _payrolls.find(payment->payroll_tag.value);
  check(payroll_itr != _payrolls.end(), "payroll with this tag/name is not registered");

  //check if payroll and payment symbols match
  check(payment->amount.symbol == payroll_itr->total_paid.quantity.symbol, "Payment token doesn't match payroll's pay token");

  //check if due date is met
  time_point_sec now = time_point_sec(current_time_point().sec_since_epoch());
  check(payment->due_date <= now, "payment not ready to be released wait "+ std::to_string(payment->due_date.sec_since_epoch() - now.sec_since_epoch() )+" sec");

  uint8_t repeat = payment->repeat;
  uint8_t current_repeated = payment->repeated+1;
  bool is_last_payment = repeat == current_repeated;

  string appended_memo = payment->memo.size() ? ": "+payment->memo : "";
  string memo = ("\""+ payment->payroll_tag.to_string()+"\" payment (ID"+to_string(pay_id)+") "+to_string(current_repeated)+" of "+to_string(repeat)+appended_memo ).c_str();
  //send the payment
  action(
    payroll_itr->pay_permission,
    payroll_itr->total_paid.contract, "transfer"_n,
    make_tuple(payroll_itr->pay_permission.actor, payment->receiver, payment->amount, memo)
  ).send();

  //update
  _payrolls.modify( payroll_itr, same_payer, [&]( auto& n) {
      n.total_paid.quantity += payment->amount;
      n.total_allocated -= payment->amount;
  });

  if(is_last_payment){
    _payments.erase(payment);
  }
  else{
    //TODO must reschedule in croneos here.

    //update the payment for the next pay
    int current_due_date_sec = payment->due_date.sec_since_epoch();
    //int delay = (current_due_date_sec - payment->submitted.sec_since_epoch())/current_repeated;
    time_point_sec next_due_date = time_point_sec(current_due_date_sec + payment->recurrence_sec);

    _payments.modify( payment, same_payer, [&]( auto& n) {
        n.due_date = next_due_date;
        n.repeated = current_repeated;
    });
  
  }
  
  _state.set(s, get_self());

}



ACTION payroll::paymentadd(name payroll_tag, name receiver, asset amount, string memo, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay){
  require_auth(get_self() );
  time_point_sec now = time_point_sec(current_time_point().sec_since_epoch());
  check(now <= due_date, "Due date must be in the future");
  check(is_account(receiver), "Receiver isn't an existing account");
  check(repeat > 0, "Repeat must be greater then zero");
  if(repeat > 1){
    check(recurrence_sec != 0, "Repeating payments need positive recurrence_sec");
  }

  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_create(get_self(), state() );

  payrolls_table _payrolls(get_self(), get_self().value);
  auto payroll_itr = _payrolls.find(payroll_tag.value);
  check(payroll_itr != _payrolls.end(), "Payroll with this tag does not exist");

  //validate amount
  check(amount.symbol == payroll_itr->total_paid.quantity.symbol, "Payroll with this tag doesn't pay with this token");
  check(amount.amount > 0 , "Amount must be greater then zero");

  _payrolls.modify( payroll_itr, same_payer, [&]( auto& n) {
     // n.total_paid.quantity += payment.amount;
      n.total_allocated += (amount*repeat);
  });

  payments_table _payments(get_self(), get_self().value);
  _payments.emplace(get_self(), [&](auto& n) {
      n.pay_id = s.next_pay_id;
      n.payroll_tag = payroll_tag;
      n.receiver = receiver;
      n.amount = amount;
      n.memo = memo;
      //n.submitted = time_point_sec(current_time_point().sec_since_epoch());
      n.due_date = due_date;
      n.repeat = repeat;
      n.recurrence_sec = recurrence_sec;
      n.auto_pay = auto_pay;
      //n.repeated=0;
  });

  s.next_pay_id++;
  _state.set(s, get_self());
}

ACTION payroll::addmany(name payroll_tag, vector<payment> payments, string memo, time_point_sec due_date, uint8_t repeat, uint64_t recurrence_sec, bool auto_pay){
  require_auth(get_self() );
  time_point_sec now = time_point_sec(current_time_point().sec_since_epoch());
  check(now <= due_date, "Due date must be in the future");
  if(repeat > 1){
    check(recurrence_sec != 0, "Repeating payments need positive recurrence_sec");
    
  }
  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_create(get_self(), state() );

  payrolls_table _payrolls(get_self(), get_self().value);
  auto payroll_itr = _payrolls.find(payroll_tag.value);
  check(payroll_itr != _payrolls.end(), "Payroll with this tag does not exist");

  payments_table _payments(get_self(), get_self().value);
  auto selected_payroll = *payroll_itr;
  asset temp_allocated = asset(0, selected_payroll.total_paid.quantity.symbol ); 
  for(payment p: payments){
    check(is_account(p.receiver), "Receiver "+p.receiver.to_string()+" isn't an existing account");
    check(p.amount.symbol == selected_payroll.total_paid.quantity.symbol, "Payroll with this tag doesn't pay with this token");
    check(p.amount.amount > 0 , "Amount must be greater then zero");

    temp_allocated += (p.amount*repeat);

    _payments.emplace(get_self(), [&](auto& n) {
        n.pay_id = s.next_pay_id;
        n.payroll_tag = payroll_tag;
        n.receiver = p.receiver;
        n.amount = p.amount;
        n.memo = memo;
        n.due_date = due_date;
        n.repeat = repeat;
        n.recurrence_sec = recurrence_sec;
        n.auto_pay = auto_pay;
    });
    s.next_pay_id++;

  }
  _payrolls.modify( payroll_itr, same_payer, [&]( auto& n) {
      n.total_allocated += temp_allocated;
  });
  _state.set(s, get_self());
}

ACTION payroll::paymentrem(uint64_t pay_id){
  require_auth(get_self() );
  payments_table _payments(get_self(), get_self().value);
  auto payment = _payments.find(pay_id);
  check(payment != _payments.end(), "payment with this id not on the payroll");

  payrolls_table _payrolls(get_self(), get_self().value);
  auto payroll_itr = _payrolls.find(payment->payroll_tag.value);
  check(payroll_itr != _payrolls.end(), "Employer with this tag/name is not registered");

  _payrolls.modify( payroll_itr, same_payer, [&]( auto& n) {
     // n.total_paid.quantity += payment.amount;
      n.total_allocated -= (payment->amount*(payment->repeat-payment->repeated));
  });

  _payments.erase(payment);
}


ACTION payroll::freeze(bool freeze){
  require_auth(get_self() );
  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );
  s.freeze_payments = freeze;
  _state.set(s, get_self());
}




