#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rhi/pipeline_key.hpp"

TEST_CASE("GraphicsPipelineKeyHash: equal keys same hash") {
    rhi::GraphicsPipelineKey a {};
    rhi::GraphicsPipelineKey b {};
    a.vert_spv_hash = 0xdeadbeefULL;
    b.vert_spv_hash = 0xdeadbeefULL;
    a.frag_spv_hash = 1;
    b.frag_spv_hash = 1;

    const rhi::GraphicsPipelineKeyHash h {};
    CHECK(h(a) == h(b));
    CHECK(a == b);
}

TEST_CASE("GraphicsPipelineKeyHash: differs on vert hash") {
    rhi::GraphicsPipelineKey a {};
    rhi::GraphicsPipelineKey b {};
    a.vert_spv_hash = 1;
    b.vert_spv_hash = 2;
    const rhi::GraphicsPipelineKeyHash h {};
    CHECK(h(a) != h(b));
    CHECK_FALSE(a == b);
}

TEST_CASE("ComputePipelineKeyHash") {
    rhi::ComputePipelineKey a { 10, 20 };
    rhi::ComputePipelineKey b { 10, 20 };
    rhi::ComputePipelineKey c { 10, 21 };
    const rhi::ComputePipelineKeyHash h {};
    CHECK(h(a) == h(b));
    CHECK(h(a) != h(c));
}
