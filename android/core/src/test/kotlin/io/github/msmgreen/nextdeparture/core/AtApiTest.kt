package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

// test/test_at_api, case for case, against the same fixtures.
class AtApiTest {
    @Test fun urls() {
        assertEquals("https://api.at.govt.nz/gtfs/v3/stops?filter%5Bstop_code%5D=8213", AtUrls.stopByCode("8213"))
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_short_name%5D=20",
            AtUrls.routesByShortName("20"),
        )
        assertEquals("https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_type%5D=2", AtUrls.railRoutes())
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips" +
                "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=17&filter%5Bhour_range%5D=3",
            AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 19), 17, 3),
        )
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/trips/20-02005-54000-2-89a317b8/stops",
            AtUrls.tripStops("20-02005-54000-2-89a317b8"),
        )
        assertEquals(
            "https://api.at.govt.nz/realtime/legacy/tripupdates?tripid=a-1,b-2",
            AtUrls.realtime(listOf("a-1", "b-2")),
        )
        assertNull(AtUrls.realtime(emptyList()))
    }

    @Test fun `start hour accepts after-midnight hours but not zero`() {
        val d = LocalDate.of(2026, 9, 19)
        assertNull(AtUrls.stoptrips("122-34ecc043", d, 0, 3))
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips" +
                "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=24&filter%5Bhour_range%5D=2",
            AtUrls.stoptrips("122-34ecc043", d, 24, 2),
        )
        assertNotNull(AtUrls.stoptrips("122-34ecc043", d, 47, 3))
        assertNull(AtUrls.stoptrips("122-34ecc043", d, 48, 3))
    }

    @Test fun `parses a stop and a station`() {
        val bus = AtParse.stops(fixture("post-crl/stops_8213.json"))!!.single()
        assertEquals("8213-7e021a72", bus.stopId)
        assertEquals(0, bus.locationType)
        val station = AtParse.stops(fixture("post-crl/stops_122.json"))!!.single()
        assertEquals("122-34ecc043", station.stopId)
        assertEquals(1, station.locationType)
        assertEquals("Wynyard Quarter", AtParse.stops(fixture("post-crl/stops_1060.json"))!!.single().stopName)
    }

    @Test fun `an unknown stop code is an empty list, not an error`() {
        assertEquals(emptyList(), AtParse.stops(fixture("post-crl/stops_unknown.json")))
        assertNull(AtParse.stops("<html>gateway timeout</html>"))
    }

    @Test fun `parses routes, colouring rail only`() {
        val bus = AtParse.routes(fixture("routes_20.json"))!!.single()
        assertEquals(RouteInfo("20-202", "20", 3, null), bus) // AT omits route_color for buses
        val rail = AtParse.routes(fixture("post-crl/routes_rail.json"))!!
        assertEquals(8, rail.size)
        assertEquals(Rgb(0x97, 0xC9, 0x3D), rail.single { it.routeId == "E-W-201" }.color)
        assertEquals(2, rail.single { it.routeId == "E-W-201" }.routeType)
        assertNull(rail.single { it.routeId == "HUIA-404" }.color) // #000000 is unusable
    }

    @Test fun `parses stoptrips`() {
        val rows = AtParse.stoptrips(fixture("post-crl/stoptrips_kingsland.json"))!!
        assertEquals(23, rows.size)
        assertEquals(12, rows.count { it.directionId == 0 })
        assertEquals(11, rows.count { it.directionId == 1 })
        // A station's rows carry the platform id.
        assertEquals(
            StopTripRow(
                "258-880001-51480-2-W118540-4f47826a", "E-W-201", "9305-ef07ca76", 0,
                LocalDate.of(2026, 9, 13), 15 * 3600 + 11 * 60,
            ),
            rows[0],
        )
    }

    @Test fun `after-midnight trips belong to the previous service date`() {
        val rows = AtParse.stoptrips(fixture("post-crl/stoptrips_kingsland_after_midnight.json"))!!
        assertEquals(11, rows.size)
        assertEquals(24 * 3600 + 4 * 60, rows[0].departureS) // "24:04:00"
        assertEquals(LocalDate.of(2026, 9, 19), rows[0].serviceDate)
        // 2026-09-20 00:04 NZST, checked with Python zoneinfo (Pacific/Auckland).
        assertEquals(1789819440, gtfsEpoch(rows[0].serviceDate, rows[0].departureS))
    }

    @Test fun `trip stops keep their sequence order`() {
        val stops = AtParse.tripStops(fixture("post-crl/trip_dir1_stops.json"))!!
        assertEquals(27, stops.size)
        assertEquals("9328", stops[0].stopCode) // Swanson, first
        assertEquals("9304-dcb2ed75", stops[12].stopId) // Kingsland platform
        assertEquals("9001", stops[16].stopCode) // Waitemata platform 1
        assertEquals("133-08da14b5", stops[16].parentStation)
    }

    @Test fun `parses realtime, stop_time_update as one object`() {
        val ents = AtParse.realtime(fixture("realtime_tripupdates_only.json"))!!
        assertEquals(6, ents.size)
        assertEquals(RtEntity("20-02005-61200-2-89a317b8", -427, false, "1060-00b64ee7", -427), ents[0])
        // This one's stop_time_update IS our Kingsland platform, and disagrees
        // with the trip-level delay: +66 against -28.
        assertEquals(RtEntity("247-810023-61200-2-9731921-77638b11", -28, false, "9304-dcb2ed75", 66), ents[4])
        // And this one has a stop_time_update with no departure at all.
        assertEquals("247-810007-60000-2-9729960-592012e4", ents[3].tripId)
        assertNull(ents[3].stuDepartureDelay)
        assertEquals(-25, ents[3].delay)
    }

    @Test fun `realtime also accepts the documented array form`() {
        val body = """{"response":{"entity":[{"trip_update":{"trip":{"trip_id":"t","schedule_relationship":3},
            "stop_time_update":[{"stop_id":"s","departure":{"delay":5}}]}}]}}"""
        val e = AtParse.realtime(body)!!.single()
        assertTrue(e.cancelled)
        assertNull(e.delay)
        assertEquals("s", e.stuStopId)
        assertEquals(5, e.stuDepartureDelay)
    }

    @Test fun `the 400 for start hour zero is an error body, not data`() {
        val body = fixture("post-crl/stoptrips_start_hour_0.json")
        assertNull(AtParse.stoptrips(body))
        assertTrue(AtParse.errorDetail(body)!!.contains("StartHour"))
        assertNull(AtParse.errorDetail(fixture("post-crl/stops_8213.json")))
    }
}
