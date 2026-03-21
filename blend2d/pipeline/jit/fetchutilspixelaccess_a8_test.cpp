// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

// A8 Pipeline — Scalar Equivalence Tests
// =======================================
//
// These tests verify the mathematical correctness of A8 (alpha-only) pixel
// operations used in satisfy_pixels_a8() and satisfy_solid_pixels_a8().
//
// The A8 pipeline uses 4 representations:
//   PA - packed alpha (u8, one byte per pixel)
//   PI - packed inverted alpha (u8, NOT(PA))
//   UA - unpacked alpha (u16, zero-extended from u8)
//   UI - unpacked inverted alpha (u16, 255 - UA)

namespace bl {
namespace Tests {

// Scalar pack: u16 → u8 (saturating pack, matches v_packs_i16_u8)
static uint8_t scalar_pack(uint16_t v) noexcept {
  return v > 255u ? 255u : uint8_t(v);
}

// Scalar unpack: u8 → u16 (zero-extend, matches v_cvt_u8_lo_to_u16)
static uint16_t scalar_unpack(uint8_t v) noexcept {
  return uint16_t(v);
}

// Scalar NOT: ~v (bitwise, matches v_not_u32 on packed bytes)
static uint8_t scalar_not_u8(uint8_t v) noexcept {
  return uint8_t(~v);
}

// Scalar inv255: 255 - v (matches v_inv255_u16)
static uint16_t scalar_inv255(uint16_t v) noexcept {
  return uint16_t(255u - v);
}

UNIT(a8_pack_unpack_roundtrip, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing pack/unpack round-trip for all 256 alpha values");
  {
    for (uint32_t a = 0; a <= 255u; a++) {
      uint8_t pa = uint8_t(a);
      uint16_t ua = scalar_unpack(pa);
      uint8_t pa_back = scalar_pack(ua);

      EXPECT_EQ(ua, uint16_t(a))
        .message("unpack(%u) = %u, expected %u", a, unsigned(ua), a);
      EXPECT_EQ(pa_back, pa)
        .message("pack(unpack(%u)) = %u, expected %u", a, unsigned(pa_back), unsigned(pa));
    }
  }

  INFO("Testing unpack preserves value in low byte, zeroes high byte");
  {
    for (uint32_t a = 0; a <= 255u; a++) {
      uint16_t ua = scalar_unpack(uint8_t(a));
      EXPECT_EQ(ua & 0xFF00u, 0u)
        .message("unpack(%u) has non-zero high byte: 0x%04X", a, unsigned(ua));
      EXPECT_EQ(ua & 0x00FFu, a)
        .message("unpack(%u) low byte mismatch: got %u", a, unsigned(ua & 0xFF));
    }
  }

  INFO("Testing pack boundary values");
  {
    EXPECT_EQ(scalar_pack(0x0000u), 0u);
    EXPECT_EQ(scalar_pack(0x00FFu), 255u);
    EXPECT_EQ(scalar_pack(0x0080u), 128u);
    // Values > 255 should saturate to 255
    EXPECT_EQ(scalar_pack(0x0100u), 255u);
    EXPECT_EQ(scalar_pack(0xFFFFu), 255u);
  }
}

UNIT(a8_pa_pi_inversion, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing PA <-> PI inversion for all 256 values");
  {
    for (uint32_t a = 0; a <= 255u; a++) {
      uint8_t pa = uint8_t(a);
      uint8_t pi = scalar_not_u8(pa);

      // PI should be bitwise NOT of PA
      EXPECT_EQ(pi, uint8_t(255u - a))
        .message("NOT(%u) = %u, expected %u", a, unsigned(pi), 255u - a);

      // NOT(PI) should give back PA
      uint8_t pa_back = scalar_not_u8(pi);
      EXPECT_EQ(pa_back, pa)
        .message("NOT(NOT(%u)) = %u, expected %u", a, unsigned(pa_back), unsigned(pa));
    }
  }

  INFO("Testing PA <-> PI boundary values");
  {
    EXPECT_EQ(scalar_not_u8(0x00u), 0xFFu);
    EXPECT_EQ(scalar_not_u8(0xFFu), 0x00u);
    EXPECT_EQ(scalar_not_u8(0x80u), 0x7Fu);
    EXPECT_EQ(scalar_not_u8(0x01u), 0xFEu);
  }
}

UNIT(a8_ua_ui_inversion, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing UA <-> UI inversion for all 256 values");
  {
    for (uint32_t a = 0; a <= 255u; a++) {
      uint16_t ua = uint16_t(a);
      uint16_t ui = scalar_inv255(ua);

      EXPECT_EQ(ui, uint16_t(255u - a))
        .message("inv255(%u) = %u, expected %u", a, unsigned(ui), 255u - a);

      // Double inversion: inv255(inv255(x)) == x
      uint16_t ua_back = scalar_inv255(ui);
      EXPECT_EQ(ua_back, ua)
        .message("inv255(inv255(%u)) = %u, expected %u", a, unsigned(ua_back), unsigned(ua));
    }
  }

  INFO("Testing UA <-> UI boundary values");
  {
    EXPECT_EQ(scalar_inv255(0u), 255u);
    EXPECT_EQ(scalar_inv255(255u), 0u);
    EXPECT_EQ(scalar_inv255(128u), 127u);
    EXPECT_EQ(scalar_inv255(1u), 254u);
  }
}

UNIT(a8_cross_format_consistency, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing cross-format consistency for all 256 alpha values");
  {
    uint32_t errors = 0;

    for (uint32_t a = 0; a <= 255u; a++) {
      // Start from PA
      uint8_t pa = uint8_t(a);

      // PA → PI via NOT
      uint8_t pi = scalar_not_u8(pa);
      EXPECT_EQ(pi, uint8_t(255u - a));

      // PA → UA via unpack
      uint16_t ua = scalar_unpack(pa);
      EXPECT_EQ(ua, uint16_t(a));

      // UA → PA via pack (round-trip)
      uint8_t pa_rt = scalar_pack(ua);
      EXPECT_EQ(pa_rt, pa);

      // UA → UI via inv255
      uint16_t ui = scalar_inv255(ua);
      EXPECT_EQ(ui, uint16_t(255u - a));

      // Consistency: PI (packed inverted) should equal pack(UI)
      uint8_t pi_from_ui = scalar_pack(ui);
      if (pi != pi_from_ui) errors++;
      EXPECT_EQ(pi, pi_from_ui)
        .message("a=%u: PI=%u but pack(UI)=%u", a, unsigned(pi), unsigned(pi_from_ui));

      // Consistency: UI (unpacked inverted) should equal unpack(PI)
      uint16_t ui_from_pi = scalar_unpack(pi);
      if (ui != ui_from_pi) errors++;
      EXPECT_EQ(ui, ui_from_pi)
        .message("a=%u: UI=%u but unpack(PI)=%u", a, unsigned(ui), unsigned(ui_from_pi));

      // Consistency: PA → unpack → inv255 should match PA → NOT → unpack
      uint16_t path1 = scalar_inv255(scalar_unpack(pa));  // unpack then inv255
      uint16_t path2 = scalar_unpack(scalar_not_u8(pa));  // NOT then unpack
      if (path1 != path2) errors++;
      EXPECT_EQ(path1, path2)
        .message("a=%u: unpack→inv255=%u but NOT→unpack=%u", a, unsigned(path1), unsigned(path2));
    }

    EXPECT_EQ(errors, 0u)
      .message("Cross-format consistency: %u errors", errors);
  }
}

UNIT(a8_satisfy_flag_combinations, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing satisfy logic: derive all flags from PA");
  {
    static constexpr uint32_t kTestAlphas[] = {0, 1, 63, 127, 128, 191, 254, 255};

    for (uint32_t a : kTestAlphas) {
      uint8_t pa = uint8_t(a);

      // Derive PI from PA
      uint8_t pi = scalar_not_u8(pa);
      EXPECT_EQ(pi, uint8_t(~pa));

      // Derive UA from PA (unpack)
      uint16_t ua = scalar_unpack(pa);
      EXPECT_EQ(ua, uint16_t(pa));

      // Derive UI from UA (inv255)
      uint16_t ui = scalar_inv255(ua);
      EXPECT_EQ(ui, uint16_t(255u - a));

      // Verify all 4 representations are consistent
      EXPECT_EQ(scalar_pack(ua), pa);
      EXPECT_EQ(scalar_pack(ui), pi);
      EXPECT_EQ(scalar_not_u8(scalar_pack(ui)), pa);
    }
  }

  INFO("Testing satisfy logic: derive all flags from UA");
  {
    static constexpr uint32_t kTestAlphas[] = {0, 1, 63, 127, 128, 191, 254, 255};

    for (uint32_t a : kTestAlphas) {
      uint16_t ua = uint16_t(a);

      // Derive PA from UA (pack)
      uint8_t pa = scalar_pack(ua);
      EXPECT_EQ(pa, uint8_t(a));

      // Derive PI from PA (NOT)
      uint8_t pi = scalar_not_u8(pa);
      EXPECT_EQ(pi, uint8_t(255u - a));

      // Derive UI from UA (inv255)
      uint16_t ui = scalar_inv255(ua);
      EXPECT_EQ(ui, uint16_t(255u - a));

      // Verify all 4 representations are consistent
      EXPECT_EQ(scalar_unpack(pa), ua);
      EXPECT_EQ(scalar_unpack(pi), ui);
    }
  }

  INFO("Testing flag derivation order independence");
  {
    // Requesting flags in different orders should produce the same result.
    for (uint32_t a = 0; a <= 255u; a++) {
      uint8_t pa = uint8_t(a);

      // Order 1: PA → PI → UA → UI
      uint8_t pi_1 = scalar_not_u8(pa);
      uint16_t ua_1 = scalar_unpack(pa);
      uint16_t ui_1 = scalar_inv255(ua_1);

      // Order 2: PA → UA → UI → PI
      uint16_t ua_2 = scalar_unpack(pa);
      uint16_t ui_2 = scalar_inv255(ua_2);
      uint8_t pi_2 = scalar_not_u8(pa);

      // Order 3: PA → UI (via unpack+inv255) → UA (via pack PI → unpack)
      uint16_t ui_3 = scalar_inv255(scalar_unpack(pa));
      uint8_t pi_3 = scalar_pack(ui_3);
      uint16_t ua_3 = scalar_unpack(pa);

      EXPECT_EQ(pi_1, pi_2);
      EXPECT_EQ(pi_1, pi_3);
      EXPECT_EQ(ua_1, ua_2);
      EXPECT_EQ(ua_1, ua_3);
      EXPECT_EQ(ui_1, ui_2);
      EXPECT_EQ(ui_1, ui_3);
    }
  }
}

UNIT(a8_solid_pixel_broadcast, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing solid PA broadcast (all bytes same value)");
  {
    static constexpr uint32_t kTestAlphas[] = {0, 1, 64, 127, 128, 191, 254, 255};

    for (uint32_t a : kTestAlphas) {
      // Solid PA is broadcast to all bytes in a u32/u128 register.
      uint32_t broadcast = uint32_t(a) * 0x01010101u;

      uint8_t b0 = uint8_t((broadcast >>  0) & 0xFFu);
      uint8_t b1 = uint8_t((broadcast >>  8) & 0xFFu);
      uint8_t b2 = uint8_t((broadcast >> 16) & 0xFFu);
      uint8_t b3 = uint8_t((broadcast >> 24) & 0xFFu);

      EXPECT_EQ(b0, uint8_t(a));
      EXPECT_EQ(b1, uint8_t(a));
      EXPECT_EQ(b2, uint8_t(a));
      EXPECT_EQ(b3, uint8_t(a));
    }
  }

  INFO("Testing solid PI == NOT(PA)");
  {
    for (uint32_t a : {0u, 1u, 64u, 127u, 128u, 191u, 254u, 255u}) {
      uint32_t pa_broadcast = a * 0x01010101u;
      uint32_t pi_broadcast = ~pa_broadcast;

      // Every byte of PI should be NOT(a)
      uint8_t expected_pi = uint8_t(~uint8_t(a));
      EXPECT_EQ(uint8_t(pi_broadcast & 0xFFu), expected_pi);
      EXPECT_EQ(uint8_t((pi_broadcast >> 8) & 0xFFu), expected_pi);
      EXPECT_EQ(uint8_t((pi_broadcast >> 16) & 0xFFu), expected_pi);
      EXPECT_EQ(uint8_t((pi_broadcast >> 24) & 0xFFu), expected_pi);
    }
  }

  INFO("Testing solid UA is correctly zero-extended from PA");
  {
    for (uint32_t a : {0u, 1u, 64u, 127u, 128u, 191u, 254u, 255u}) {
      // UA from PA via cvt_u8_lo_to_u16: each byte becomes a u16
      uint16_t ua = scalar_unpack(uint8_t(a));
      EXPECT_EQ(ua, uint16_t(a));
      EXPECT_TRUE(ua <= 255u);
    }
  }

  INFO("Testing solid UI == 255 - UA");
  {
    for (uint32_t a : {0u, 1u, 64u, 127u, 128u, 191u, 254u, 255u}) {
      uint16_t ua = uint16_t(a);
      uint16_t ui = scalar_inv255(ua);
      EXPECT_EQ(ui, uint16_t(255u - a));
    }
  }

  INFO("Testing requesting all 4 solid flags produces consistent values");
  {
    for (uint32_t a = 0; a <= 255u; a++) {
      uint8_t pa = uint8_t(a);
      uint8_t pi = scalar_not_u8(pa);
      uint16_t ua = scalar_unpack(pa);
      uint16_t ui = scalar_inv255(ua);

      // All representations must be self-consistent
      EXPECT_EQ(scalar_pack(ua), pa);
      EXPECT_EQ(scalar_pack(ui), pi);
      EXPECT_EQ(scalar_unpack(pi), ui);
      EXPECT_EQ(scalar_inv255(ui), ua);
    }
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
