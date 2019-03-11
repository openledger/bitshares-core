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

   asset_object create_and_issue_uia(const std::string& asset_name, const account_object& issuer, uint32_t count)
   {
      additional_asset_options options;

      const auto uia = create_user_issued_asset(asset_name, issuer, charge_market_fee,
         price(asset(1, asset_id_type(1)), asset(1)),
         1, 20 * GRAPHENE_1_PERCENT, options);

      issue_uia(issuer, uia.amount(count));
      return uia;
   }

   void transfer_uia(const std::vector<account_object>& accounts, const account_id_type from,const asset& amount)
   {
      for (auto& account: accounts)
      {
         transfer(committee_account, account.get_id(), asset(1000000));
         transfer(from, account.get_id(), amount);
      }
   }

   void create_sell_orders(const std::vector<account_object>& accounts, uint32_t iterations, const asset& asset_to_sell, const asset& asset_to_buy)
   {
      const auto count = accounts.size();
      for (unsigned int i = 0; i < iterations; ++i)
      {
         for (unsigned int j = 0; j < count; ++j)
         {
            create_sell_order(accounts[j], asset_to_sell, asset_to_buy);
            create_sell_order(accounts[count - j - 1], asset_to_buy, asset_to_sell);
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

      const auto accounts = 3000;
      const auto iterations = 20;
      const auto uia_to_sell = 2000;

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME - 1000);

      const auto ui_asset = create_and_issue_uia("UIASSET", issuer, iterations * accounts * uia_to_sell );
      const auto traders = create_accounts(accounts);
      transfer_uia(traders, issuer_id, ui_asset.amount(iterations * uia_to_sell));

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();
      create_sell_orders(traders, iterations, ui_asset.amount(uia_to_sell), asset(1));

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      for (const auto& account : traders)
      {
         const auto statistic = get_trade_statistics(db, account.get_id(), ui_asset.get_id());
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

      const auto accounts = 3000;
      const auto iterations = 20;
      const auto uia_to_sell = 2000;

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);

      const auto ui_asset = create_and_issue_uia("UIASSET", issuer, iterations * accounts * uia_to_sell );
      const auto traders = create_accounts(accounts);
      transfer_uia(traders, issuer_id, ui_asset.amount(iterations * uia_to_sell));

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();
      create_sell_orders(traders, iterations, ui_asset.amount(uia_to_sell), asset(1));

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("performance_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test took: ${c} ms", ("c", elapsed.count()));

      for (const auto& account : traders)
      {
         const auto statistic = get_trade_statistics(db, account.get_id(), ui_asset.get_id());
         BOOST_CHECK_GT(statistic->total_volume.amount, 0);
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()

