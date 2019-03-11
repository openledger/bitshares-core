#include <boost/test/unit_test.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include "../common/database_fixture.hpp"


using namespace graphene::chain;
using namespace graphene::chain::test;
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

struct dynamic_market_fee_database_fixture : database_fixture
{
   const share_type core_precision = asset::scaled_precision( asset_id_type()(db).precision );

   dynamic_market_fee_database_fixture()
   : database_fixture(HARDFORK_DYNAMIC_FEE_TIME - 100 )
   {}

   asset core_asset(int64_t x )
   {
       return asset( x*core_precision );
   };

   void issue_asset()
   {
      try
      {
         ACTORS((alice)(bob)(izzy)(jill));

         fund( alice, core_asset(1000000) );
         fund( bob, core_asset(1000000) );
         fund( izzy, core_asset(1000000) );
         fund( jill, core_asset(1000000) );

         price price(asset(1, asset_id_type(1)), asset(1));

         asset_object izzycoin = create_user_issued_asset( "IZZYCOIN", izzy,  charge_market_fee, price, 2 );

         asset_object jillcoin = create_user_issued_asset( "JILLCOIN", jill,  charge_market_fee, price, 2 );

         // Alice and Bob create some coins
         issue_uia( alice, izzycoin.amount( 100000 ) );
         issue_uia( bob, jillcoin.amount( 100000 ) );
      }
      FC_LOG_AND_RETHROW()
   }
};

BOOST_FIXTURE_TEST_SUITE( dynamic_market_fee_tests, dynamic_market_fee_database_fixture )

BOOST_AUTO_TEST_CASE(adjust_trade_statistics_test_before_HARDFORK_DYNAMIC_FEE_TIME_hf)
{
   try
   {
      issue_asset();
      return;
      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );
      create_sell_order(   bob_id, jillcoin.amount(300), izzycoin.amount(100) );
      {
         const auto alice_izzycoin_tso = get_trade_statistics(db, alice_id, izzycoin.get_id());
         const auto alice_jillcoin_tso = get_trade_statistics(db, alice_id, jillcoin.get_id());

         BOOST_CHECK(alice_izzycoin_tso == nullptr);
         BOOST_CHECK(alice_jillcoin_tso == nullptr);

         const auto bob_izzycoin_tso = get_trade_statistics(db, bob_id, izzycoin.get_id());
         const auto bob_jillcoin_tso = get_trade_statistics(db, bob_id, jillcoin.get_id());

         BOOST_CHECK(bob_izzycoin_tso == nullptr);
         BOOST_CHECK(bob_jillcoin_tso == nullptr);
      }
   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(adjust_trade_statistics_test_after_HARDFORK_DYNAMIC_FEE_TIME_hf)
{
   try
   {
      issue_asset();

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      set_expiration( db, trx );

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );
      create_sell_order( bob_id, jillcoin.amount(300), izzycoin.amount(100) );
      {
         const auto alice_jillcoin_tso = get_trade_statistics(db, alice_id, jillcoin.get_id());
         BOOST_CHECK_EQUAL(alice_jillcoin_tso->total_volume.amount, 300);

         const auto bob_izzycoin_tso = get_trade_statistics(db, bob_id, izzycoin.get_id());
         BOOST_CHECK_EQUAL(bob_izzycoin_tso->total_volume.amount, 100);
      }
      // Alice and Bob place orders which match
      // trade_statistics should be updated
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );
      create_sell_order( bob_id, jillcoin.amount(300), izzycoin.amount(100) );
      {
         const auto alice_jillcoin_tso = get_trade_statistics(db, alice_id, jillcoin.get_id());
         BOOST_CHECK_EQUAL(alice_jillcoin_tso->total_volume.amount, 600);

         const auto bob_izzycoin_tso = get_trade_statistics(db, bob_id, izzycoin.get_id());
         BOOST_CHECK_EQUAL(bob_izzycoin_tso->total_volume.amount, 200);
      }
   }
   FC_LOG_AND_RETHROW()
}
BOOST_AUTO_TEST_SUITE_END()