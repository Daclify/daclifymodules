#pragma once
#include <eosio/singleton.hpp>

#define _CONTRACT_NAME_ "hooks"


struct [[eosio::table, eosio::contract(_CONTRACT_NAME_)]] moduleconfig {
        //required fields for module config
        eosio::name modulename;
        eosio::name parent;

        //optional custom fields

};


typedef eosio::singleton<"moduleconfig"_n, moduleconfig> moduleconfig_table;


class Module{
        moduleconfig_table _moduleconfig;
        moduleconfig conf;
        eosio::name self;

        Module(eosio::name self_, eosio::name module_name, eosio::name parent): _moduleconfig(self_, self_.value)  { 
            self = self_;
            conf = _moduleconfig.get_or_create(self, moduleconfig() );
            if(conf.modulename.value==0){
                conf.modulename = module_name;
            }
            if(conf.parent.value==0){
                conf.parent = parent;
            }
        }

        Module(eosio::name self): _moduleconfig(self, self.value)  { 
            self = self;
            conf = _moduleconfig.get_or_create(self, moduleconfig() );
        }

        ~Module(){
            _moduleconfig.set(conf, self);
        }
};