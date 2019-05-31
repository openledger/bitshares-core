#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/app/database_api.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>


#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;


namespace fc
{
   template<typename Ch, typename T>
   std::basic_ostream<Ch>& operator<<(std::basic_ostream<Ch>& os, safe<T> const& sf)
   {
      os << sf.value;
      return os;
   }
}

struct stock_database_fixture : database_fixture
{
   stock_database_fixture()
      : database_fixture(HARDFORK_STOCK_ASSET_TIME - 100)
   {
   }

   void update_asset( const account_id_type& issuer_id,
                      const fc::ecc::private_key& private_key,
                      const asset_id_type& asset_id,
                      const flat_set<asset_id_type>& revenue_assets )
   {
      asset_update_operation op;
      op.issuer = issuer_id;
      op.asset_to_update = asset_id;
      op.new_options = asset_id(db).options;
      op.new_options.extensions.value.reward_percent = 0;
      op.new_options.extensions.value.revenue_assets = revenue_assets;

      signed_transaction tx;
      tx.operations.push_back( op );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, private_key );
      PUSH_TX( db, tx );
   }

   void generate_blocks()
   {
      database_fixture::generate_blocks( HARDFORK_STOCK_ASSET_TIME );
      while( db.head_block_time() <= HARDFORK_STOCK_ASSET_TIME )
      {
         database_fixture::generate_block();
      }
   }

   void wait_for_maintenance() {
      database_fixture::generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      database_fixture::generate_block();
    }
};

BOOST_FIXTURE_TEST_SUITE( stock_tests, stock_database_fixture )

BOOST_AUTO_TEST_CASE(stock_asset_test)
{
   try
   {
      ACTORS( (registrar)(holder)(izzy)(jill) )
      //ACTORS((registrar)(alicereferrer)(bobreferrer)(izzy)(jill));

      auto register_account = [&](const string& name, const account_object& referrer) -> const account_object&
      {
         uint8_t referrer_percent  = 100;
         fc::ecc::private_key _private_key = generate_private_key(name);
         public_key_type _public_key = _private_key.get_public_key();
         return create_account(name, registrar, referrer, referrer_percent, _public_key);
      };

      // izzy issues asset to seller1
      // jill issues asset to seller2
      // seller1 and seller2 trade in the market and pay fees
      upgrade_to_lifetime_member(registrar);
      upgrade_to_lifetime_member(holder);
      upgrade_to_lifetime_member(izzy);
      upgrade_to_lifetime_member(jill);

      auto seller1 = register_account("seller1", izzy);
      auto seller2 = register_account("seller2", jill);

      const share_type core_prec = asset::scaled_precision( asset_id_type()(db).precision );

      // Return number of core shares (times precision)
      auto _core = [&]( int64_t x ) -> asset
      {  return asset( x*core_prec );    };

      transfer( committee_account, seller1.id, _core(1000000) );
      transfer( committee_account, seller2.id, _core(1000000) );
      transfer( committee_account, holder_id,  _core(1000000) );
      transfer( committee_account, izzy_id,    _core(1000000) );
      transfer( committee_account, jill_id,    _core(1000000) );

      //create assets
      constexpr auto izzycoin_market_percent = 10*GRAPHENE_1_PERCENT;
      constexpr auto jillcoin_market_percent = 20*GRAPHENE_1_PERCENT;

      asset_id_type izzycoin_id = create_bitasset( "IZZYCOIN", izzy_id, izzycoin_market_percent ).id;
      asset_id_type jillcoin_id = create_bitasset( "JILLCOIN", jill_id, jillcoin_market_percent ).id;

      const share_type izzy_prec = asset::scaled_precision( asset_id_type(izzycoin_id)(db).precision );
      const share_type jill_prec = asset::scaled_precision( asset_id_type(jillcoin_id)(db).precision );

      //create stock asset (revenue asset only IZZYCOIN)
      constexpr auto stock_asset_market_percent = 10*GRAPHENE_1_PERCENT;
      asset_id_type stockcoin_id = create_bitasset( "STOCKCOIN", holder_id, stock_asset_market_percent ).id;

      const share_type stockcoin_prec = asset::scaled_precision( asset_id_type(stockcoin_id)(db).precision );

      GRAPHENE_REQUIRE_THROW( update_asset(holder_id, holder_private_key, stockcoin_id, {izzycoin_id}), fc::exception );
      generate_blocks();
      update_asset(holder_id, holder_private_key, stockcoin_id, {izzycoin_id} );

      auto _izzy = [&]( int64_t x ) -> asset
      {   return asset( x*izzy_prec, izzycoin_id );   };
      auto _jill = [&]( int64_t x ) -> asset
      {   return asset( x*jill_prec, jillcoin_id );   };
      auto _stockcoin = [&]( int64_t x ) -> asset
      {   return asset( x*stockcoin_prec, stockcoin_id );   };

      update_feed_producers( izzycoin_id(db), { izzy_id } );
      update_feed_producers( jillcoin_id(db), { jill_id } );
      update_feed_producers( stockcoin_id(db), { holder_id } );

      // Izzycoin is worth 100 BTS
      price_feed feed;
      feed.settlement_price = price( _izzy(1), _core(100) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( izzycoin_id(db), izzy, feed );

      // Jillcoin is worth 30 BTS
      feed.settlement_price = price( _jill(1), _core(30) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( jillcoin_id(db), jill, feed );

      // Stockcoin is worth 100 BTS
      feed.settlement_price = price( _stockcoin(1), _core(100) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( stockcoin_id(db), holder, feed );

      enable_fees();

      // seller1 and seller2 create some coins
      borrow( seller1.id, _izzy( 1500 ), _core( 600000 ) );
      borrow( seller2.id, _jill( 2000 ), _core( 180000 ) );
      borrow( holder_id,  _stockcoin( 1500 ), _core( 600000 ) );

      const asset_object& test_asset = izzycoin_id(db);
      const asset_dynamic_data_object& test_asset_dynamic_data = test_asset.dynamic_asset_data_id(db);

      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees == 0);

      // Alice and Bob place orders which match
      create_sell_order( seller1.id, _izzy(1000), _jill(1500) );
      create_sell_order( seller2.id, _jill(1500), _izzy(1000) );

      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees != 0);

      share_type holder_reward = get_market_fee_reward( holder_id, izzycoin_id );
      BOOST_CHECK( holder_reward == 0 );

      wait_for_maintenance();

      holder_reward = get_market_fee_reward( holder_id, izzycoin_id );
      BOOST_CHECK( holder_reward != 0 );
   }
   FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_SUITE_END()