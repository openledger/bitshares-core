/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>
#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::detail;

namespace graphene { namespace chain {namespace detail {
   const trade_statistics_object* get_trade_statistics(const database &db, const account_id_type& account_id, const asset_id_type &asset_id);
   uint64_t calculate_percent(const share_type& value, uint16_t percent);
}}}

namespace fc
{
   template<typename Ch, typename T>
   std::basic_ostream<Ch>& operator<<(std::basic_ostream<Ch>& os, safe<T> const& sf)
   {
      os << sf.value;
      return os;
   }
}

struct dmf_performance_fixture : database_fixture
{
   // How many traders to create
   const static auto traders_count = 100;
   // Count of cycles for trading
   const static auto iterations = 1;
   // How many assets to sell/buy
   const static auto uia_to_sell = 20;
   // How many assets to create
   const static auto ui_assets_count = 100;

   // without verify_asset_supplies
   void fast_upgrade_to_lifetime_member( const account_object& account )
   {
      try
      {
         account_upgrade_operation op;
         op.account_to_upgrade = account.get_id();
         op.upgrade_to_lifetime_member = true;
         op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
         trx.operations = {op};
         PUSH_TX(db, trx, ~0);
         FC_ASSERT( op.account_to_upgrade(db).is_lifetime_member() );
         trx.clear();
      }
      FC_CAPTURE_AND_RETHROW((account))
   }

   // without verify_asset_supplies
   void fast_transfer(const account_id_type& from, const account_id_type& to, const asset& amount, const asset& fee = asset() )
   {
      try
      {
         test::set_expiration( db, trx );
         transfer_operation trans;
         trans.from = from;
         trans.to   = to;
         trans.amount = amount;
         trx.operations.push_back(trans);

         if( fee == asset() )
         {
            for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
         }
         trx.validate();
         PUSH_TX(db, trx, ~0);
         trx.operations.clear();
      } FC_CAPTURE_AND_RETHROW( (from)(to)(amount)(fee) )
   }

     // without verify_asset_supplies
   const limit_order_object* fast_create_sell_order( const account_object& user, const asset& amount, const asset& recv,
                                                const time_point_sec order_expiration = time_point_sec::maximum(),
                                                const price& fee_core_exchange_rate = price::unit_price() )
   {
      test::set_expiration( db, trx );
      trx.operations.clear();

      limit_order_create_operation buy_order;
      buy_order.seller = user.id;
      buy_order.amount_to_sell = amount;
      buy_order.min_to_receive = recv;
      buy_order.expiration = order_expiration;
      trx.operations.push_back(buy_order);
      for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op, fee_core_exchange_rate);
      trx.validate();
      auto processed = PUSH_TX(db, trx, ~0);
      trx.operations.clear();
      return db.find<limit_order_object>( processed.operation_results[0].get<object_id_type>() );
   }

   using accounts_t = std::vector<account_object>;
   accounts_t create_accounts(uint32_t count)
   {
      std::vector<account_object> registrators;
      for (unsigned int i = 0; i < count; ++i)
      {
         auto account = create_account("registrar" + std::to_string(i));
         fast_transfer(committee_account, account.get_id(), asset(1000000));
         fast_upgrade_to_lifetime_member(account);

         registrators.push_back(std::move(account));
      }

      std::vector<account_object> traders;
      for (unsigned int i = 0; i < count; ++i)
      {
         std::string name = "account" + std::to_string(i);
         auto account = create_account(name, registrators[i], registrators[i], GRAPHENE_1_PERCENT);
         traders.push_back(std::move(account));
      }
      return traders;
   }

   asset_object create_and_issue_uia(const std::string& asset_name, const account_object& issuer, 
                                     share_type count, bool dynamic = false)
   {
      additional_asset_options_t options;
      uint16_t flags = charge_market_fee;

      if (dynamic)
      {
         dynamic_fee_table fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT}, {50000, 30}}, 
                                        .taker_fee = {{0,GRAPHENE_1_PERCENT}, {50000, 45}}};
         options.value.dynamic_fees = fee_table;
         flags |= charge_dynamic_market_fee;
      }

      const auto uia = create_user_issued_asset(
                           asset_name, issuer, 
                           flags,
                           price(asset(1, asset_id_type(1)), asset(1)),
                           1, 20 * GRAPHENE_1_PERCENT, options);

      issue_uia(issuer, uia.amount(count));
      return uia;
   }

   using ui_assets_t = std::vector<asset_object>;
   ui_assets_t create_and_issue_uia(const account_object& issuer, uint32_t count, const share_type& amount, bool dynamic = false)
   {
      ui_assets_t  uia_list;
      for (uint32_t i = 0; i < count; ++i)
      {
         uia_list.emplace_back(create_and_issue_uia("UIASSET" + std::to_string(i), issuer, amount, dynamic));
      }
      return uia_list;
   }

   void transfer_uia(const accounts_t& accounts, const account_id_type from,const asset& amount)
   {
      for (auto& account: accounts)
      {
         fast_transfer(committee_account, account.get_id(), asset(1000));
         fast_transfer(from, account.get_id(), amount);
      }
   }

   void transfer_uia(const accounts_t& accounts, 
                     const account_id_type from,
                     const ui_assets_t& uia_list,
                     const share_type& amount)
   {
      for (const auto& account : accounts)
      {
         for (const auto& uia : uia_list)
         {
            fast_transfer(from, account.get_id(), uia.amount(amount));
         }
      }
   }

   void generate_accounts(int count)
   {
      for (auto i = 0; i < count; ++i )
      {
         db.create<account_object>( [&](account_object& account) {
            account.membership_expiration_date = time_point_sec::maximum();
            account.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
            account.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
            account.owner.weight_threshold = 1;
            account.active.weight_threshold = 1;
            account.name = "account-" + std::to_string(i);

            account.statistics = db.create<account_statistics_object>( [&account](account_statistics_object& stat) {
               stat.owner = account.id;
               stat.name = account.name;
               stat.core_in_balance = GRAPHENE_MAX_SHARE_SUPPLY;
            }).id;
         });
      }
   }

   void initialize_trade_statistics(const asset_id_type& asset_id, uint32_t start_with_account_id)
   {
      auto block_time = db.head_block_time();

      const auto max_statistics_noise = start_with_account_id + 10000000UL;
      for (uint64_t instance = start_with_account_id; instance < max_statistics_noise; ++instance)
      {
         db.create<trade_statistics_object>([&instance, &asset_id, &block_time](trade_statistics_object &o) {
            o.account_id = account_id_type(instance);
            o.total_volume = asset(0, asset_id);
            o.first_trade_date = block_time;
         });
      }
   }

   void create_sell_orders(const accounts_t& traders, const ui_assets_t& assets, uint32_t iterations, const share_type& amount)
   {
      const auto traders_count = traders.size();

      for (unsigned int i = 0; i < iterations; ++i)
      {
         for (unsigned int j = 0; j < traders_count; ++j)
         {
            const auto& maker          = traders[j];
            const auto& taker          = traders[traders_count - j - 1];

            const auto assets_count = assets.size();

            for (unsigned int k = 0; k < assets_count; ++k)
            {
               const auto& asset_to_sell  = assets[k].amount(amount);
               const auto& asset_to_buy   = assets[assets_count - k - 1].amount(amount);

               fast_create_sell_order(maker, asset_to_sell, asset_to_buy);
               fast_create_sell_order(taker, asset_to_buy, asset_to_sell);
            }
         }
      }
   }
};

BOOST_FIXTURE_TEST_SUITE( dmf_performance_test, dmf_performance_fixture )

BOOST_AUTO_TEST_CASE(performance_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test)
{
   try
   {
      ACTORS((issuer));

      generate_block();

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME - 1000);

      generate_accounts(1000000);
      const auto uia_list = create_and_issue_uia(issuer, ui_assets_count, 2 * iterations * traders_count * uia_to_sell);

      const auto traders = create_accounts(traders_count);
      transfer_uia(traders, issuer_id, uia_list, 2 * iterations * uia_to_sell);

      initialize_trade_statistics(asset_id_type(), traders.back().get_id().instance.value + 1);

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();

      create_sell_orders(traders, uia_list, iterations, uia_to_sell);

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      const auto expected_fees = 2 * traders_count * calculate_percent(uia_to_sell * iterations, 20 * GRAPHENE_1_PERCENT);

      for (const auto& uia : uia_list)
      {
         BOOST_REQUIRE_EQUAL(uia.dynamic_asset_data_id(db).accumulated_fees, expected_fees );
         for (const auto& trader : traders)
         {
            const auto statistic = get_trade_statistics(db, trader.get_id(), uia.get_id());
            BOOST_REQUIRE(statistic == nullptr);
         }
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(performance_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test)
{
   try
   {
      ACTORS((issuer));

      generate_block();

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);

      generate_accounts(1000000);
      const auto uia_list = create_and_issue_uia(issuer, ui_assets_count, 2 * iterations * traders_count * uia_to_sell, true);

      const auto traders = create_accounts(traders_count);
      transfer_uia(traders, issuer_id, uia_list, 2 * iterations * uia_to_sell);

      initialize_trade_statistics(asset_id_type(), traders.back().get_id().instance.value + 1);

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();
      create_sell_orders(traders, uia_list, iterations, uia_to_sell);

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      const auto expected_volume_per_trader = 2 * (uia_to_sell * iterations);
      const auto expected_fees = calculate_percent(expected_volume_per_trader, GRAPHENE_1_PERCENT);

      for (const auto& uia : uia_list)
      {
         BOOST_REQUIRE_EQUAL(uia.dynamic_asset_data_id(db).accumulated_fees, expected_fees );
         for (const auto& trader : traders)
         {
            const auto statistic = get_trade_statistics(db, trader.get_id(), uia.get_id());
            BOOST_REQUIRE_EQUAL(statistic->total_volume.amount, expected_volume_per_trader);
         }
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
