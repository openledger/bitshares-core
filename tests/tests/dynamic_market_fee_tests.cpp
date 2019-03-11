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

         price price(asset(1, asset_id_type(1)), asset(1));

         asset_object izzycoin = create_user_issued_asset( "IZZYCOIN", izzy,  charge_market_fee, price, 2 );

         asset_object jillcoin = create_user_issued_asset( "JILLCOIN", jill,  charge_market_fee, price, 2 );

         // Alice and Bob create some coins
         issue_uia( alice, izzycoin.amount( 100000 ) );
         issue_uia( bob, jillcoin.amount( 100000 ) );
      }
      FC_LOG_AND_RETHROW()
   }

   asset_create_operation get_create_operation() const
   {
      asset_create_operation creator;
      creator.issuer = account_id_type();
      creator.fee = asset();
      creator.symbol = UIA_TEST_SYMBOL;
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      creator.common_options.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
      creator.common_options.flags = charge_market_fee;
      return creator;
   }

   void create_uia()
   {
    try
      {
         trx.operations.push_back(std::move(get_create_operation()));
         PUSH_TX( db, trx, ~0 );
      }
      FC_LOG_AND_RETHROW()
   }
};

BOOST_FIXTURE_TEST_SUITE( dynamic_market_fee_tests, dynamic_market_fee_database_fixture )

BOOST_AUTO_TEST_CASE( adjust_trade_statistics_test_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
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

BOOST_AUTO_TEST_CASE( adjust_trade_statistics_test_after_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
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

BOOST_AUTO_TEST_CASE( create_uia_dmf_table_not_available_before_HARDFORK_DYNAMIC_FEE_TIME_hf_test )
{
   try {
      dynamic_fee_table fee_table = {.maker_fee = {{0,10}, {2, 30}}, .taker_fee = {{0,10}, {20, 30}}};
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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
      additional_asset_options options = {.dynamic_fees = fee_table};

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
      additional_asset_options options = {.dynamic_fees = fee_table};
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

BOOST_AUTO_TEST_SUITE_END()