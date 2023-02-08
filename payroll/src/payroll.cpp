#include <payroll.hpp>

/**
 * The 'payrollreg' action creates a new payroll record in the table, and sets the total_paid and total_allocated to 0
 * 
 * @param payroll_tag The name of the payroll.
 * @param pay_from The account that will be used to pay the employees.
 * @param xfer_permission The permission that the payroll contract will use to transfer funds from the
 * pay_from account.
 * @param payment_token The token that will be used to pay employees.
 * @param description A description of the payroll.
 */
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

/**
 * The 'payrollrem' action removes a payroll from the payrolls table
 * 
 * @param payroll_tag The tag of the payroll to be removed.
 */
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

/**
 * The `payrolldesc` action allows the contract owner to update the description of a payroll
 * 
 * @param payroll_tag The name of the payroll you want to update.
 * @param description The description of the payroll.
 */
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


/**
 * The 'pay' action pays a particular payment (by id) if the payment due-date has been reached
 * 
 * @param pay_id the id of the payment to be paid
 */
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



/**
 * The 'paymentadd' action adds a payment to the payments table
 * 
 * @param payroll_tag The tag of the payroll you want to add the payment to.
 * @param receiver The account that will receive the payment
 * @param amount The amount of tokens to be paid.
 * @param memo A string that will be included in the memo of the transfer action.
 * @param due_date The date and time when the payment is due.
 * @param repeat How many times the payment should be repeated.
 * @param recurrence_sec The number of seconds between each payment.
 * @param auto_pay If true, the payment will be automatically paid when the due date is reached. If
 * false, the payment will be paid when the pay action is called.
 */
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

/**
 * The 'addmany' action adds (optionally repeating) payments to the payments table, and increments the next_pay_id
 * 
 * @param payroll_tag The tag of the payroll you want to use
 * @param payments a vector of payments, each containing a receiver and an amount
 * @param memo A string that will be attached to the memo of the transfer action.
 * @param due_date The date the payment is due.
 * @param repeat How many times to repeat the payment.
 * @param recurrence_sec The number of seconds between each payment.
 * @param auto_pay If true, the payment will be automatically paid when the due date is reached.
 */
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

/**
 * The 'paymentrem' action removes a payment from the payroll
 * 
 * @param pay_id The id of the payment to be removed.
 */
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


/**
 * The `freeze` action allows the contract owner to freeze the contract, preventing any further payments from
 * being made
 * 
 * @param freeze true to freeze payments, false to unfreeze payments
 */
ACTION payroll::freeze(bool freeze){
  require_auth(get_self() );
  state_table _state(get_self(), get_self().value);
  auto s = _state.get_or_default(state() );
  s.freeze_payments = freeze;
  _state.set(s, get_self());
}




