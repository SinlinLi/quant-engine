#include "test/test_framework.h"
#include "core/symbol_table.h"

TEST(symbol_id_allocation) {
    qe::SymbolTable st;
    ASSERT_EQ(st.id("BTCUSDT"), (uint16_t)0);
    ASSERT_EQ(st.id("ETHUSDT"), (uint16_t)1);
    ASSERT_EQ(st.id("SOLUSDT"), (uint16_t)2);
    ASSERT_EQ(st.size(), (uint16_t)3);
}

TEST(symbol_id_idempotent) {
    qe::SymbolTable st;
    auto id1 = st.id("BTCUSDT");
    auto id2 = st.id("BTCUSDT");
    ASSERT_EQ(id1, id2);
    ASSERT_EQ(st.size(), (uint16_t)1);
}

TEST(symbol_name_lookup) {
    qe::SymbolTable st;
    st.id("BTCUSDT");
    st.id("ETHUSDT");
    ASSERT_TRUE(st.name(0) == "BTCUSDT");
    ASSERT_TRUE(st.name(1) == "ETHUSDT");
}

TEST(symbol_name_out_of_range) {
    qe::SymbolTable st;
    ASSERT_THROWS(st.name(0));
    st.id("X");
    ASSERT_THROWS(st.name(1));
}

TEST(symbol_contains) {
    qe::SymbolTable st;
    ASSERT_FALSE(st.contains("BTCUSDT"));
    st.id("BTCUSDT");
    ASSERT_TRUE(st.contains("BTCUSDT"));
    ASSERT_FALSE(st.contains("ETHUSDT"));
}