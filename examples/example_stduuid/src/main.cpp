#include <algorithm>
#include <array>
#include <cassert>
#include <print>
#include <random>
#include <set>
#include <string>
#include <unordered_set>

#include <stduuid/uuid.h>

std::string getUuidVariantString(uuids::uuid_variant uuidVariant) {
    if (uuidVariant == uuids::uuid_variant::ncs) {
        return "ncs";
    }
    if (uuidVariant == uuids::uuid_variant::rfc) {
        return "rfc";
    }
    if (uuidVariant == uuids::uuid_variant::microsoft) {
        return "microsoft";
    }
    if (uuidVariant == uuids::uuid_variant::reserved) {
        return "reserved";
    }
    return "Unknown UUID Variant";
}

std::string getUuidVersionString(uuids::uuid_version uuidVersion) {

    if (uuidVersion == uuids::uuid_version::none) {
        return "none";
    }
    if (uuidVersion == uuids::uuid_version::time_based) {
        return "time_based";
    }
    if (uuidVersion == uuids::uuid_version::dce_security) {
        return "dce_security";
    }
    if (uuidVersion == uuids::uuid_version::name_based_md5) {
        return "name_based_md5";
    }
    if (uuidVersion == uuids::uuid_version::random_number_based) {
        return "random_number_based";
    }
    if (uuidVersion == uuids::uuid_version::name_based_sha1) {
        return "name_based_sha1";
    }
    return "Unknown UUID Version";
}

int main(int argc, char *argv[]) {

    {
        uuids::uuid empty;
        assert(empty.is_nil());
        auto t = empty.version();

        std::println("is nil: {}, variant: {}, version: {}", empty.is_nil(),
                     getUuidVariantString(empty.variant()),
                     getUuidVersionString(empty.version()));
    }

    {
        uuids::uuid const id = uuids::uuid_system_generator {}();
        assert(!id.is_nil());
        assert(id.version() == uuids::uuid_version::random_number_based);
        assert(id.variant() == uuids::uuid_variant::rfc);
    }

    {
        std::random_device rd;
        auto seed_data = std::array<int, std::mt19937::state_size> {};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937 generator(seq);
        uuids::uuid_random_generator gen { generator };

        uuids::uuid const id = gen();
        assert(!id.is_nil());
        assert(id.as_bytes().size() == 16);
        assert(id.version() == uuids::uuid_version::random_number_based);
        assert(id.variant() == uuids::uuid_variant::rfc);
    }

    {
        std::random_device rd;
        auto seed_data = std::array<int, 6> {};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::ranlux48_base generator(seq);

        uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);
        uuids::uuid const id = gen();
        assert(!id.is_nil());
        assert(id.as_bytes().size() == 16);
        assert(id.version() == uuids::uuid_version::random_number_based);
        assert(id.variant() == uuids::uuid_variant::rfc);
    }

    {
        std::random_device rd;
        auto seed_data = std::array<int, 6> {};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::ranlux48_base generator(seq);

        uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);
        uuids::uuid const id = gen();
        assert(!id.is_nil());
        assert(id.as_bytes().size() == 16);
        assert(id.version() == uuids::uuid_version::random_number_based);
        assert(id.variant() == uuids::uuid_variant::rfc);
    }

    {
        uuids::uuid_name_generator gen(
            uuids::uuid::from_string("47183823-2574-4bfd-b411-99ed177d3e43")
                .value());
        uuids::uuid const id = gen("john");

        assert(!id.is_nil());
        assert(id.version() == uuids::uuid_version::name_based_sha1);
        assert(id.variant() == uuids::uuid_variant::rfc);
    }

    {
        using namespace std::string_literals;

        auto str = "47183823-2574-4bfd-b411-99ed177d3e43"s;
        auto id = uuids::uuid::from_string(str);
        assert(id.has_value());
        assert(uuids::to_string(id.value()) == str);

        // or

        // auto str = L"47183823-2574-4bfd-b411-99ed177d3e43"s;
        // uuids::uuid id = uuids::uuid::from_string(str).value();
        // assert(uuids::to_string<wchar_t>(id) == str);
    }

    {
        // std::array<uuids::uuid::value_type, 16> arr {
        //     { 0x47, 0x18, 0x38, 0x23, 0x25, 0x74, 0x4b, 0xfd, 0xb4, 0x11,
        //     0x99,
        //       0xed, 0x17, 0x7d, 0x3e, 0x43 }
        // };
        // uuids::uuid id(arr);

        // assert(uuids::to_string(id) ==
        // "47183823-2574-4bfd-b411-99ed177d3e43");

        // or

        // uuids::uuid::value_type arr[16] = { 0x47, 0x18, 0x38, 0x23, 0x25,
        // 0x74,
        //                                     0x4b, 0xfd, 0xb4, 0x11, 0x99,
        //                                     0xed, 0x17, 0x7d, 0x3e, 0x43 };
        // uuids::uuid id(std::begin(arr), std::end(arr));
        // assert(uuids::to_string(id) ==
        // "47183823-2574-4bfd-b411-99ed177d3e43");

        // or

        uuids::uuid id { { 0x47, 0x18, 0x38, 0x23, 0x25, 0x74, 0x4b, 0xfd, 0xb4,
                           0x11, 0x99, 0xed, 0x17, 0x7d, 0x3e, 0x43 } };

        assert(uuids::to_string(id) == "47183823-2574-4bfd-b411-99ed177d3e43");
    }

    {
        uuids::uuid empty;
        uuids::uuid id = uuids::uuid_system_generator {}();
        assert(empty == empty);
        assert(id == id);
        assert(empty != id);
    }

    {
        uuids::uuid empty;
        uuids::uuid id = uuids::uuid_system_generator {}();

        assert(empty.is_nil());
        assert(!id.is_nil());

        std::swap(empty, id);

        assert(!empty.is_nil());
        assert(id.is_nil());

        empty.swap(id);

        assert(empty.is_nil());
        assert(!id.is_nil());
    }

    {
        uuids::uuid empty;
        assert(uuids::to_string(empty) ==
               "00000000-0000-0000-0000-000000000000");
        assert(uuids::to_string<wchar_t>(empty) ==
               L"00000000-0000-0000-0000-000000000000");
    }

    {
        std::random_device rd;
        auto seed_data = std::array<int, std::mt19937::state_size> {};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937 engine(seq);
        uuids::uuid_random_generator gen(&engine);

        std::set<uuids::uuid> ids { uuids::uuid {}, gen(), gen(), gen(),
                                    gen() };

        assert(ids.size() == 5);
        assert(ids.find(uuids::uuid {}) != ids.end());
    }

    {
        std::random_device rd;
        auto seed_data = std::array<int, std::mt19937::state_size> {};
        std::ranges::generate(seed_data, std::ref(rd));
        std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
        std::mt19937 engine(seq);
        uuids::uuid_random_generator gen(&engine);

        std::unordered_set<uuids::uuid> ids { uuids::uuid {}, gen(), gen(),
                                              gen(), gen() };

        assert(ids.size() == 5);
        assert(ids.find(uuids::uuid {}) != ids.end());
    }

    {
        using namespace std::string_literals;
        auto str = "47183823-2574-4bfd-b411-99ed177d3e43"s;
        uuids::uuid id = uuids::uuid::from_string(str).value();

        auto h1 = std::hash<std::string> {};
        auto h2 = std::hash<uuids::uuid> {};
        assert(h1(str) == h2(id));
    }

    return 0;
}
