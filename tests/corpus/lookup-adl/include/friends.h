#pragma once
class /*@Account_fwd*/Account;
class /*@Bank*/Bank {
 public:
  friend class /*@fr_Account*/Account;
  friend void /*@fr_audit*/audit(Bank&);
  int /*@Bank_get*/get() const { return secret; }
 private:
  int /*@Bank_secret*/secret = 42;
};
class /*@Account*/Account {
 public:
  int peek(Bank& b) { return b./*@use_secret_acct*/secret; }
};
void /*@audit_def*/audit(Bank& b);
