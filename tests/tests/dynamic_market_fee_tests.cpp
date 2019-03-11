#include <boost/test/unit_test.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::chain::detail;

#define UIA_TEST_SYMBOL "UIATEST"

namespace graphene { namespace chain {namespace detail {
   const trade_statistics_object* get_trade_statistics(const database &db, const account_id_type& account_id, const asset_id_type &asset_id);
   void adjust_trade_statistics(database &db, const account_id_type& account_id, const asset& amount);
   int get_sliding_statistic_window_days();
   uint64_t calculate_percent(const share_type& value, uint16_t percent);
}}}

namespace graphene { namespace chain {
   bool operator == (const dynamic_fee_table::dynamic_fee& lhs, const dynamic_fee_table::dynamic_fee& rhs)
   {
      return (lhs.amount == rhs.amount) && (lhs.percent == rhs.percent);
   }
}}

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

         create_uia( izzy_id, "IZZYCOIN" );
         create_uia( jill_id, "JILLCOIN" );

         // Alice and Bob create some coins
         issue_uia( alice, get_asset("IZZYCOIN").amount( 100000 ) );
         issue_uia( bob, get_asset("JILLCOIN").amount( 100000 ) );
      }
      FC_LOG_AND_RETHROW()
   }

   void update_asset_to_dynamic(const string& name, const dynamic_fee_table& table, uint32_t max_fee = 10000000)
   {
      try
      {
         additional_asset_options options = {{}, {}, table};
         const auto& uia = get_asset(name);

         asset_update_operation op;
         op.issuer = uia.issuer;
         op.asset_to_update = uia.id;
         op.new_options = uia.options;
         op.new_options.extensions.value = options;
         op.new_options.flags |= charge_dynamic_market_fee;
         op.new_options.max_market_fee = max_fee;
         trx.operations.push_back(std::move(op));
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      FC_LOG_AND_RETHROW()
   }

   void update_dynamic_asset_to_classic(const string& name,
                                        const uint16_t fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100,
                                        uint32_t max_fee = 10000000)
   {
      try
      {
         const auto& uia = get_asset(name);

         asset_update_operation op;
         op.issuer = uia.issuer;
         op.asset_to_update = uia.id;
         op.new_options = uia.options;
         op.new_options.extensions.value = {};
         op.new_options.flags = charge_market_fee;
         op.new_options.max_market_fee = max_fee;
         op.new_options.market_fee_percent = fee_percent;
         trx.operations.push_back(std::move(op));
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      FC_LOG_AND_RETHROW()
   }

   asset_create_operation get_create_operation(const account_id_type issuer = account_id_type(),
                                               const string& name_asset = UIA_TEST_SYMBOL) const
   {
      asset_create_operation creator;
      creator.issuer = issuer;
      creator.fee = asset();
      creator.symbol = name_asset;
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.max_market_fee = 100000000;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      creator.common_options.core_exchange_rate = price(asset(1),asset(1,asset_id_type(1)));
      creator.common_options.flags = charge_market_fee;
      return creator;
   }

   void create_uia(const account_id_type issuer = account_id_type(),
                   const string& name_asset = UIA_TEST_SYMBOL)
   {
    try
      {
         trx.operations.push_back(std::move(get_create_operation(issuer, name_asset)));
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      FC_LOG_AND_RETHROW()
   }

   asset_update_operation get_update_operation(const string& name) const
   {
      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {10000, 30}}, .taker_fee = {{0,10}, {20000, 45}}};
      additional_asset_options options = {{}, {}, fee_table};

      const auto& uia = get_asset(name);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.extensions.value = options;
      op.new_options.flags |= charge_dynamic_market_fee;
      return op;
   }

   void update_uia_to_dynamic(const string& name)
   {
    try
      {
         trx.operations.push_back(std::move(get_update_operation(name)));
         PUSH_TX( db, trx, ~0 );
         trx.operations.clear();
      }
      FC_LOG_AND_RETHROW()
   }

   void update_uia_to_dynamic_with_market_sharing(const string& name, uint16_t dynamic_percent, uint16_t reward_percent)
   { try {
      const auto& uia = get_asset(name);

      asset_update_operation op;

      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags |= (charge_dynamic_market_fee | charge_market_fee);

      dynamic_fee_table fee_table = { .maker_fee = {{0,dynamic_percent}}, .taker_fee = {{0,dynamic_percent}} };
      op.new_options.extensions.value = {reward_percent, {}, fee_table}; 

      trx.operations = {op};
      PUSH_TX( db, trx, ~0 );
      trx.operations.clear();

   } FC_LOG_AND_RETHROW() }
};

BOOST_FIXTURE_TEST_SUITE( dynamic_market_fee_tests, dynamic_market_fee_database_fixture )

BOOST_AUTO_TEST_CASE( adjust_trade_statistics_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try
   {
      issue_asset();
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

BOOST_AUTO_TEST_CASE( adjust_trade_statistics_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try
   {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      update_uia_to_dynamic("JILLCOIN");
      update_uia_to_dynamic("IZZYCOIN");

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

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

BOOST_AUTO_TEST_CASE( adjust_trade_statistics_only_for_dynamic_asset_test )
{
   try
   {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      update_uia_to_dynamic("JILLCOIN");

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );
      create_sell_order( bob_id, jillcoin.amount(300), izzycoin.amount(100) );
      {
         const auto alice_jillcoin_tso = get_trade_statistics(db, alice_id, jillcoin.get_id());
         BOOST_CHECK_EQUAL(alice_jillcoin_tso->total_volume.amount, 300);

         const auto bob_izzycoin_tso = get_trade_statistics(db, bob_id, izzycoin.get_id());
         BOOST_CHECK(bob_izzycoin_tso == nullptr);
      }
      // Alice and Bob place orders which match
      // trade_statistics should be updated
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(300) );
      create_sell_order( bob_id, jillcoin.amount(300), izzycoin.amount(100) );
      {
         const auto alice_jillcoin_tso = get_trade_statistics(db, alice_id, jillcoin.get_id());
         BOOST_CHECK_EQUAL(alice_jillcoin_tso->total_volume.amount, 600);

         const auto bob_izzycoin_tso = get_trade_statistics(db, bob_id, izzycoin.get_id());
         BOOST_CHECK(bob_izzycoin_tso == nullptr);
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( create_uia_dmf_table_not_available_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_flag_not_available_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_to_create_uia_dmf_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_to_create_uia_dmf_without_fee_table_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_to_create_uia_dmf_without_flag_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_id_type test_asset_id = db.get_index<asset_object>().get_next_id();

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK((test_asset.options.flags & charge_dynamic_market_fee) == charge_dynamic_market_fee);
      BOOST_CHECK(test_asset.options.extensions.value.dynamic_fees->maker_fee == fee_table.maker_fee);
      BOOST_CHECK(test_asset.options.extensions.value.dynamic_fees->taker_fee == fee_table.taker_fee);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_update_uia_dmf_without_flag_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_update_uia_dmf_without_fee_table_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_update_uia_dmf_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      create_uia();

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      trx.operations.clear();
      trx.operations.push_back(op);

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( not_allowed_update_uia_dmf_without_flag_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( not_allowed_update_uia_dmf_without_fee_table_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      trx.operations.clear();
      trx.operations.push_back(op);

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};
      {
         const auto& uia = get_asset(UIA_TEST_SYMBOL);

         asset_update_operation op;
         op.issuer = uia.issuer;
         op.asset_to_update = uia.id;
         op.new_options = uia.options;
         op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
         op.new_options.extensions.value = options;
         trx.operations.clear();
         trx.operations.push_back(op);
         PUSH_TX( db, trx, ~0 );
      }

      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      BOOST_CHECK((uia.options.flags & charge_dynamic_market_fee) == charge_dynamic_market_fee);
      BOOST_CHECK(uia.options.extensions.value.dynamic_fees->maker_fee == fee_table.maker_fee);
      BOOST_CHECK(uia.options.extensions.value.dynamic_fees->taker_fee == fee_table.taker_fee);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_maker_non_zero_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table = {.maker_fee = {{10,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_taker_non_zero_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{1,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_taker_non_zero_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{1,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_maker_non_zero_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{1,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( create_uia_dmf_with_empty_maker_table_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {};
      fee_table.taker_fee = {{0,10}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_empty_taker_table_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{0,10}};
      fee_table.taker_fee = {};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_empty_maker_table_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {};
      fee_table.taker_fee = {{1,10}, {2, 30}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_empty_taker_table_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{1,10}, {2, 30}};
      fee_table.taker_fee = {};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_incorrect_taker_percent_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{0,10}};
      fee_table.taker_fee = {{0,10001}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_incorrect_taker_percent_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 10001}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_incorrect_taker_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{0,10}};
      fee_table.taker_fee = {{-10,10000}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_incorrect_taker_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {-20, 100}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_incorrect_maker_percent_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{0,10002}};
      fee_table.taker_fee = {{0,10}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_incorrect_maker_percent_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 10002}}, .taker_fee = {{0,10}, {20, 100}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( create_uia_dmf_with_incorrect_maker_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      dynamic_fee_table fee_table;
      fee_table.maker_fee = {{-1,10000}};
      fee_table.taker_fee = {{0,10}};
      additional_asset_options options = {{}, {}, fee_table};

      asset_create_operation creator = std::move(get_create_operation());
      creator.common_options.flags = charge_dynamic_market_fee;
      creator.common_options.extensions.value = options;
      trx.operations.push_back(std::move(creator));

      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_uia_dmf_with_incorrect_maker_amount_test )
{
   try {
      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      create_uia();

      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {-2, 10}}, .taker_fee = {{0,10}, {20, 100}}};
      additional_asset_options options = {{}, {}, fee_table};
      const auto& uia = get_asset(UIA_TEST_SYMBOL);

      asset_update_operation op;
      op.issuer = uia.issuer;
      op.asset_to_update = uia.id;
      op.new_options = uia.options;
      op.new_options.flags = op.new_options.flags | charge_dynamic_market_fee;
      op.new_options.extensions.value = options;
      trx.operations.clear();
      trx.operations.push_back(op);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( sliding_windows_interval_test )
{
   try {
      ACTORS((alice));
      generate_block();

      auto check_statistics = [&](const account_id_type& account_id, const asset_id_type &asset_id, const share_type expected_amount) {
         auto statistic = get_trade_statistics(db, account_id, asset_id);
         BOOST_CHECK_EQUAL(statistic->total_volume.amount, expected_amount);
      };

      adjust_trade_statistics(db, alice_id, asset{20, asset_id_type{1}});
      adjust_trade_statistics(db, alice_id, asset{60, asset_id_type{2}});

      check_statistics(alice_id, asset_id_type{1}, 20);
      check_statistics(alice_id, asset_id_type{2}, 60);

      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      check_statistics(alice_id, asset_id_type{1}, 20);
      check_statistics(alice_id, asset_id_type{2}, 60);

      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time + fc::days(get_sliding_statistic_window_days()) );
      check_statistics(alice_id, asset_id_type{1}, 19);
      check_statistics(alice_id, asset_id_type{2}, 58);

      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time + fc::days(2 * get_sliding_statistic_window_days()) );
      check_statistics(alice_id, asset_id_type{1}, 18);
      check_statistics(alice_id, asset_id_type{2}, 56);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( user_pays_dynamic_fee_that_shared_to_registrar )
{
   auto dynamic_and_sharing_time = std::max(HARDFORK_1268_TIME, HARDFORK_DYNAMIC_FEE_TIME);
   generate_blocks(dynamic_and_sharing_time);
   set_expiration( db, trx );

   issue_asset();

   GET_ACTOR(alice);
   GET_ACTOR(bob);

   auto dynamic_percent = 200;
   auto reward_percent = 4000;
   update_uia_to_dynamic_with_market_sharing("JILLCOIN", dynamic_percent, reward_percent);

   const asset_object &jillcoin = get_asset("JILLCOIN");
   const asset_object &izzycoin = get_asset("IZZYCOIN");

   // Alice and Bob place orders which match
   create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(1000) );
   create_sell_order( bob_id, jillcoin.amount(1000), izzycoin.amount(100) );

   const auto expected_fees = 1000 * dynamic_percent / GRAPHENE_100_PERCENT;
   const auto expected_rewards = expected_fees * reward_percent / GRAPHENE_100_PERCENT;
   share_type alice_registrar_reward = get_market_fee_reward( alice.registrar, jillcoin.get_id() );

   BOOST_CHECK_EQUAL(alice_registrar_reward, expected_rewards);
}

BOOST_AUTO_TEST_CASE( apply_dynamic_fee_corresponding_trade_statistics_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      // setup dynamic fee table as follows {.maker_fee = {{0,10}, {10000, 30}}, .taker_fee = {{0,10}, {20000, 45}}};
      update_uia_to_dynamic("JILLCOIN");

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      {
         const auto dyn_pcts = db.get_dynamic_market_fee_percent(alice_id, jillcoin);
         const pair<uint16_t, uint16_t> expected = {10, 10};
         // check for initial values, see: update_uia_to_dynamic()
         BOOST_CHECK(dyn_pcts == expected);
      }

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(1000) );
      create_sell_order( bob_id, jillcoin.amount(1000), izzycoin.amount(100) );
      {
         const auto dyn_pcts = db.get_dynamic_market_fee_percent(alice_id, jillcoin);
         const pair<uint16_t, uint16_t> expected = {10, 10};
         BOOST_CHECK(dyn_pcts == expected);
      }

      // Alice and Bob place orders which match
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(19000) );
      create_sell_order( bob_id, jillcoin.amount(19000), izzycoin.amount(100) );
      {
         const auto dyn_pcts = db.get_dynamic_market_fee_percent(alice_id, jillcoin);
         const pair<uint16_t, uint16_t> expected = {30, 45};
         BOOST_CHECK(dyn_pcts == expected);
      }
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( apply_dynamic_fee_percent_after_first_order_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      uint16_t maker_percent = GRAPHENE_1_PERCENT*20;
      uint16_t taker_percent = GRAPHENE_1_PERCENT*30;
      int64_t order_amount = 2000;
      dynamic_fee_table jillcoin_fee_table = {.maker_fee = {{0,maker_percent}, {order_amount - 1, GRAPHENE_1_PERCENT*10}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*60}}};
      update_asset_to_dynamic("JILLCOIN", jillcoin_fee_table);

      dynamic_fee_table izzycoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*50}},
                                              .taker_fee = {{0,taker_percent}, {order_amount - 10, GRAPHENE_1_PERCENT*10}}};
      update_asset_to_dynamic("IZZYCOIN", izzycoin_fee_table);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );

      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ), order_amount - calculate_percent(order_amount, maker_percent));
      BOOST_CHECK_EQUAL(get_balance( bob, izzycoin ), order_amount - calculate_percent(order_amount, taker_percent));

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( apply_dynamic_fee_percent_hit_on_limit_interval_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      int64_t  limit_amount = 1000;
      uint16_t limit_maker_percent = GRAPHENE_1_PERCENT*10;
      dynamic_fee_table jillcoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*20}, {limit_amount,limit_maker_percent}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*40}}};
      update_asset_to_dynamic("JILLCOIN", jillcoin_fee_table);

      uint16_t limit_taker_percent = GRAPHENE_1_PERCENT*30;
      dynamic_fee_table izzycoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*60}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*30}, {limit_amount,limit_taker_percent}}};
      update_asset_to_dynamic("IZZYCOIN", izzycoin_fee_table);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - taker, bob - maker)
      create_sell_order( bob_id, jillcoin.amount(limit_amount), izzycoin.amount(limit_amount) );
      create_sell_order( alice_id, izzycoin.amount(limit_amount), jillcoin.amount(limit_amount) );

      const int64_t alice_balance_after_first_bidding = get_balance( alice, jillcoin );
      const int64_t bob_balance_after_first_bidding = get_balance( bob, izzycoin );

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(100) );
      create_sell_order( bob_id, jillcoin.amount(100), izzycoin.amount(100) );

      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ),
                                     alice_balance_after_first_bidding + 100 - calculate_percent(100, limit_maker_percent));
      BOOST_CHECK_EQUAL(get_balance( bob, izzycoin ),
                                     bob_balance_after_first_bidding + 100 - calculate_percent(100, limit_taker_percent));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( apply_dynamic_fee_percent_hit_on_interval_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      int64_t  limit_amount = 1000;
      int64_t  first_order_amount = limit_amount+500;
      uint16_t limit_maker_percent = GRAPHENE_1_PERCENT*20;
      uint16_t limit_taker_percent = GRAPHENE_1_PERCENT*30;
      dynamic_fee_table jillcoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*30}, {limit_amount,limit_maker_percent},
                                                            {first_order_amount+1, GRAPHENE_1_PERCENT*10}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*10}}};
      update_asset_to_dynamic("JILLCOIN", jillcoin_fee_table);

      dynamic_fee_table izzycoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*10}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*40}, {limit_amount, limit_taker_percent},
                                                            {first_order_amount+1, GRAPHENE_1_PERCENT*10}}};
      update_asset_to_dynamic("IZZYCOIN", izzycoin_fee_table);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - taker, bob - maker)
      create_sell_order( bob_id, jillcoin.amount(first_order_amount), izzycoin.amount(first_order_amount) );
      create_sell_order( alice_id, izzycoin.amount(first_order_amount), jillcoin.amount(first_order_amount) );

      const int64_t alice_balance_after_first_bidding = get_balance( alice, jillcoin );
      const int64_t bob_balance_after_first_bidding = get_balance( bob, izzycoin );

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(100), jillcoin.amount(100) );
      create_sell_order( bob_id, jillcoin.amount(100), izzycoin.amount(100) );

      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ),
                                     alice_balance_after_first_bidding + 100 - calculate_percent(100, limit_maker_percent));
      BOOST_CHECK_EQUAL(get_balance( bob, izzycoin ),
                                     bob_balance_after_first_bidding + 100 - calculate_percent(100, limit_taker_percent));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( apply_dynamic_fee_percent_for_more_than_max_fee_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      uint16_t maker_percent = GRAPHENE_1_PERCENT*50;
      uint16_t taker_percent = GRAPHENE_1_PERCENT*40;
      int64_t  order_amount = 2000;
      uint32_t max_fee = 100;
      dynamic_fee_table jillcoin_fee_table = {.maker_fee = {{0,maker_percent}, {order_amount - 1, GRAPHENE_1_PERCENT*10}},
                                              .taker_fee = {{0,GRAPHENE_1_PERCENT*60}}};
      update_asset_to_dynamic("JILLCOIN", jillcoin_fee_table, max_fee);

      dynamic_fee_table izzycoin_fee_table = {.maker_fee = {{0,GRAPHENE_1_PERCENT*50}},
                                              .taker_fee = {{0,taker_percent}, {order_amount - 1, GRAPHENE_1_PERCENT*10}}};
      update_asset_to_dynamic("IZZYCOIN", izzycoin_fee_table, max_fee);

      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );

      BOOST_CHECK_GT(calculate_percent(order_amount, maker_percent), max_fee);
      BOOST_CHECK_GT(calculate_percent(order_amount, taker_percent), max_fee);

      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ), order_amount - max_fee);
      BOOST_CHECK_EQUAL(get_balance( bob, izzycoin ), order_amount - max_fee);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_dynamic_fee_percent_for_maker_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      const uint16_t fifty_maker_percent = GRAPHENE_1_PERCENT*50;
      const uint16_t thirty_maker_percent = GRAPHENE_1_PERCENT*30;
      const uint16_t taker_percent = GRAPHENE_1_PERCENT*60;
      int64_t  order_amount = 2000;
      {
         dynamic_fee_table fee_table = {.maker_fee = {{0,fifty_maker_percent}, {order_amount - 1, thirty_maker_percent}},
                                        .taker_fee = {{0,taker_percent}}};
         update_asset_to_dynamic("JILLCOIN", fee_table);
      }
      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );

      const int64_t alice_balance_after_first_bidding = get_balance( alice, jillcoin );

      BOOST_CHECK_EQUAL(alice_balance_after_first_bidding, order_amount - calculate_percent(order_amount, fifty_maker_percent));

      {
         const auto dyn_pcts = db.get_dynamic_market_fee_percent(alice_id, jillcoin);
         const pair<uint16_t, uint16_t> expected = {thirty_maker_percent, taker_percent};
         BOOST_CHECK(dyn_pcts == expected);
      }

      {
         dynamic_fee_table fee_table = {.maker_fee = {{0,fifty_maker_percent}, {order_amount + 1, thirty_maker_percent}},
                                        .taker_fee = {{0,taker_percent}}};
         update_asset_to_dynamic("JILLCOIN", fee_table);
      }

      // Alice and Bob place orders which match (alice - maker, bob - taker)
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );

      const int64_t expected_balance = alice_balance_after_first_bidding + order_amount - calculate_percent(order_amount, fifty_maker_percent);
      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ), expected_balance);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_dynamic_fee_percent_for_taker_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      const uint16_t fifty_taker_percent = GRAPHENE_1_PERCENT*50;
      const uint16_t thirty_taker_percent = GRAPHENE_1_PERCENT*30;
      const uint16_t maker_percent = GRAPHENE_1_PERCENT*60;
      int64_t  order_amount = 2000;
      {
         dynamic_fee_table fee_table = {.maker_fee = {{0,maker_percent}},
                                        .taker_fee = {{0,fifty_taker_percent}, {order_amount - 1, thirty_taker_percent}}};
         update_asset_to_dynamic("JILLCOIN", fee_table);
      }
      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - taker, bob - maker)
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );

      const int64_t alice_balance_after_first_bidding = get_balance( alice, jillcoin );

      BOOST_CHECK_EQUAL(alice_balance_after_first_bidding, order_amount - calculate_percent(order_amount, fifty_taker_percent));

      {
         const auto dyn_pcts = db.get_dynamic_market_fee_percent(alice_id, jillcoin);
         const pair<uint16_t, uint16_t> expected = {maker_percent, thirty_taker_percent};
         BOOST_CHECK(dyn_pcts == expected);
      }

      {
         dynamic_fee_table fee_table = {.maker_fee = {{0,maker_percent}},
                                        .taker_fee = {{0,fifty_taker_percent}, {order_amount + 1, thirty_taker_percent}}};
         update_asset_to_dynamic("JILLCOIN", fee_table);
      }

      // Alice and Bob place orders which match (alice - taker, bob - maker)
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );

      const int64_t expected_balance = alice_balance_after_first_bidding + order_amount - calculate_percent(order_amount, fifty_taker_percent);
      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ), expected_balance);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( update_dmf_asset_to_uia_test )
{
   try {
      issue_asset();

      GET_ACTOR(alice);
      GET_ACTOR(bob);

      generate_blocks(HARDFORK_DYNAMIC_FEE_TIME);
      set_expiration( db, trx );

      const uint16_t taker_percent = GRAPHENE_1_PERCENT*50;
      const uint16_t maker_percent = GRAPHENE_1_PERCENT*60;
      const uint16_t uia_jillcoin_fee_percent = GRAPHENE_1_PERCENT*2;
      const uint16_t uia_izzycoin_fee_percent = GRAPHENE_1_PERCENT*4;
      int64_t  order_amount = 2000;
      {
         dynamic_fee_table fee_table = {.maker_fee = {{0, maker_percent}},
                                        .taker_fee = {{0, taker_percent}}};
         update_asset_to_dynamic("JILLCOIN", fee_table);
         update_asset_to_dynamic("IZZYCOIN", fee_table);
      }
      const asset_object &jillcoin = get_asset("JILLCOIN");
      const asset_object &izzycoin = get_asset("IZZYCOIN");

      // Alice and Bob place orders which match (alice - taker, bob - maker)
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );

      const int64_t alice_balance_after_first_bidding = get_balance( alice, jillcoin );
      const int64_t bob_balance_after_first_bidding = get_balance( bob, izzycoin );

      BOOST_CHECK_EQUAL(alice_balance_after_first_bidding, order_amount - calculate_percent(order_amount, taker_percent));
      BOOST_CHECK_EQUAL(bob_balance_after_first_bidding, order_amount - calculate_percent(order_amount, maker_percent));

      update_dynamic_asset_to_classic("JILLCOIN", uia_jillcoin_fee_percent);
      update_dynamic_asset_to_classic("IZZYCOIN", uia_izzycoin_fee_percent);

      // Alice and Bob place orders which match
      create_sell_order( bob_id, jillcoin.amount(order_amount), izzycoin.amount(order_amount) );
      create_sell_order( alice_id, izzycoin.amount(order_amount), jillcoin.amount(order_amount) );

      const int64_t expected_alice_balance = alice_balance_after_first_bidding + order_amount - calculate_percent(order_amount, uia_jillcoin_fee_percent);
      BOOST_CHECK_EQUAL(get_balance( alice, jillcoin ), expected_alice_balance);

      const int64_t expected_bob_balance = bob_balance_after_first_bidding + order_amount - calculate_percent(order_amount, uia_izzycoin_fee_percent);
      BOOST_CHECK_EQUAL(get_balance( bob, izzycoin ), expected_bob_balance);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()