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
   std::vector<account_object> create_accounts(uint32_t count)
   {
      std::vector<account_object> registrators;
      for (unsigned int i = 0; i < count; ++i)
      {
         auto account = create_account("registrar" + std::to_string(i));
         transfer(committee_account, account.get_id(), asset(1000000));
         upgrade_to_lifetime_member(account);

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

   void transfer_uia(const std::vector<account_object>& accounts, const account_id_type from,const asset& amount)
   {
      for (auto& account: accounts)
      {
         transfer(committee_account, account.get_id(), asset(1000));
         transfer(from, account.get_id(), amount);
      }
   }

   using balances_t = std::vector<std::pair<account_object, asset_object>>;
   balances_t transfer_uia(const std::vector<account_object>& accounts,
                           const account_id_type from,
                           const ui_assets_t& uia_list,
                           const share_type& amount)
   {
      balances_t balances;
      auto uia_it = uia_list.begin();
      assert(uia_it != uia_list.end());

      for (const auto& account : accounts)
      {
         transfer(committee_account, account.get_id(), asset(1000));
         transfer(from, account.get_id(), uia_it->amount(amount));
         balances.emplace_back(std::make_pair(account, *uia_it));

         ++uia_it;
         if (uia_it == uia_list.end())
         {
            uia_it = uia_list.begin();
         }
      }
      return balances;
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

   void create_sell_orders(const balances_t& balances, uint32_t iterations, const share_type& amount)
   {
      const auto count = balances.size();
      for (unsigned int i = 0; i < iterations; ++i)
      {
         for (unsigned int j = 0; j < count; ++j)
         {
            const auto& maker          = balances[j].first;
            const auto& taker          = balances[count - j - 1].first;
            const auto& asset_to_sell  = balances[j].second.amount(amount);
            const auto& asset_to_buy   = asset(1);

            create_sell_order(maker, asset_to_sell, asset_to_buy);
            create_sell_order(taker, asset_to_buy, asset_to_sell);
         }
      }
   }

   std::map<asset_id_type, int> uia_factors(const ui_assets_t& uia_list, int accounts)
   {
      const size_t total_uia_cycles = accounts / uia_list.size();
      const size_t rest_of_assets = accounts % uia_list.size();
      std::map<asset_id_type, int> factors;

      size_t i = 1;
      for(const auto &uia: uia_list)
      {
         if (i > uia_list.size())
         {
            i = 1;
         }

         const auto factor = (i > rest_of_assets) ? total_uia_cycles : total_uia_cycles + 1;
         factors[uia.get_id()] = factor;
         ++i;
      }
      return factors;
   }
};

BOOST_FIXTURE_TEST_SUITE( dmf_performance_test, dmf_performance_fixture )

BOOST_AUTO_TEST_CASE(performance_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test)
{
   try
   {
      ACTORS((issuer));

      generate_block();

      const auto accounts = 1000;
      const auto iterations = 1;
      const auto uia_to_sell = 2000;
      const auto ui_assets = 100;

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME - 1000);

      generate_accounts(1000000);
      const auto uia_list = create_and_issue_uia(issuer, ui_assets, iterations * accounts * uia_to_sell);

      const auto traders = create_accounts(accounts);
      const auto balances = transfer_uia(traders, issuer_id, uia_list, iterations * uia_to_sell);

      initialize_trade_statistics(asset_id_type(), traders.back().get_id().instance.value + 1);

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();

      create_sell_orders(balances, iterations, uia_to_sell);

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      const auto expected_fees = calculate_percent(uia_to_sell * iterations, 20 * GRAPHENE_1_PERCENT);
      auto factors = uia_factors(uia_list, accounts);

      for (const auto& account_uia_pair : balances)
      {
         BOOST_REQUIRE_EQUAL(account_uia_pair.second.dynamic_asset_data_id(db).accumulated_fees, expected_fees * factors[account_uia_pair.second.get_id()]);
         const auto statistic = get_trade_statistics(db, account_uia_pair.first.get_id(), account_uia_pair.second.get_id());
         BOOST_REQUIRE(statistic == nullptr);
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

      const auto accounts = 1000;
      const auto iterations = 1;
      const auto uia_to_sell = 2000;
      const auto ui_assets = 100;

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);

      generate_accounts(1000000);
      const auto uia_list = create_and_issue_uia(issuer, ui_assets, iterations * accounts * uia_to_sell, true);

      const auto traders = create_accounts(accounts);
      const auto balances = transfer_uia(traders, issuer_id, uia_list, iterations * uia_to_sell);

      initialize_trade_statistics(asset_id_type(), traders.back().get_id().instance.value + 1);

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();
      create_sell_orders(balances, iterations, uia_to_sell);

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      const auto expected_volume_per_trader = (uia_to_sell * iterations);
      const auto expected_fees = calculate_percent(expected_volume_per_trader, GRAPHENE_1_PERCENT);

      auto factors = uia_factors(uia_list, accounts);

      auto rit = balances.rbegin();
      for (const auto& account_uia_pair : balances)
      {
         BOOST_REQUIRE_EQUAL(account_uia_pair.second.dynamic_asset_data_id(db).accumulated_fees, expected_fees * factors[account_uia_pair.second.get_id()] );
         const auto statistic = get_trade_statistics(db, account_uia_pair.first.get_id(), rit->second.get_id());
         BOOST_REQUIRE_EQUAL(statistic->total_volume.amount, expected_volume_per_trader);
         ++rit;
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()

