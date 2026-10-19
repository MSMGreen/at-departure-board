package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

// test/test_color's parse_hex cases.
class RouteColorTest {
    @Test fun `accepts bare, hashed and lowercase`() {
        assertEquals(Rgb(151, 201, 61), parseHex("97C93D"))
        assertEquals(Rgb(151, 201, 61), parseHex("#97c93d"))
        assertEquals(Rgb(151, 201, 61), parseHex("  97C93D "))
    }

    @Test fun `treats black as absent`() {
        assertNull(parseHex("000000")) // HUIA: invisible on a dark ground
    }

    @Test fun `rejects garbage`() {
        for (s in listOf(null, "", "zz", "12345", "1234567", "GGGGGG")) assertNull(parseHex(s), "$s")
    }

    @Test fun `packs ARGB for Android`() {
        assertEquals(0xFF97C93D.toInt(), Rgb(0x97, 0xC9, 0x3D).argb)
    }
}
