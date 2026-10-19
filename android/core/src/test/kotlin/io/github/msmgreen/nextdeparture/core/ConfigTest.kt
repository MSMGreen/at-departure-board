package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class ConfigTest {
    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val ok = Config(places = listOf(home))

    private fun error(c: Config) = ConfigCodec.validate(c)

    private fun withWatch(w: WatchConfig) = Config(places = listOf(home.copy(watch = w)))

    @Test fun `round-trips through JSON with the board's field names`() {
        val text = ConfigCodec.toJson(ok)
        for (k in listOf("\"v\":1", "\"stop_code\"", "\"route_short_name\"", "\"toward_stop_code\"", "\"walk_min\"")) {
            assertTrue(k in text, "$k in $text")
        }
        assertEquals(ConfigResult.Ok(ok), ConfigCodec.parse(text))
    }

    @Test fun `defaults fill what is left out`() {
        val text = """{"v":1,"places":[{"name":"Work","watch":{"label":"home","stop_code":"1060","toward_stop_code":"8213"}}]}"""
        val c = (ConfigCodec.parse(text) as ConfigResult.Ok).config
        assertEquals("", c.places[0].watch.routeShortName) // any route
        assertEquals(0, c.places[0].walkMin)
        assertEquals(200, c.places[0].radiusM)
    }

    @Test fun `garbage and other versions are told apart from a bad field`() {
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse(""))
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse("{\"v\":1"))
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse("{\"v\":1}"))
        assertEquals(ConfigResult.Bad(ConfigError.BadVersion), ConfigCodec.parse(ConfigCodec.toJson(ok.copy(v = 2))))
    }

    @Test fun `place counts`() {
        assertEquals(ConfigError.NoPlaces, error(Config(places = emptyList())))
        assertEquals(null, error(Config(places = List(4) { home })))
        assertEquals(ConfigError.TooManyPlaces, error(Config(places = List(5) { home })))
    }

    @Test fun `names and labels`() {
        assertEquals(ConfigError.Name, error(Config(places = listOf(home.copy(name = " ")))))
        assertEquals(ConfigError.Name, error(Config(places = listOf(home.copy(name = "x".repeat(13))))))
        assertEquals(null, error(withWatch(home.watch.copy(label = ""))))
        assertEquals(ConfigError.Label, error(withWatch(home.watch.copy(label = "x".repeat(25)))))
    }

    @Test fun `stop codes are plain ASCII letters and digits`() {
        for (bad in listOf("", "82 13", "8213;", "٨٢١٣", "x".repeat(12))) {
            assertEquals(ConfigError.StopCode, error(withWatch(home.watch.copy(stopCode = bad))), bad)
        }
    }

    @Test fun `the toward stop is required, unlike on the board`() {
        assertEquals(ConfigError.Toward, error(withWatch(home.watch.copy(towardStopCode = ""))))
    }

    @Test fun `routes may carry a dash, as rail short names do`() {
        assertEquals(null, error(withWatch(home.watch.copy(routeShortName = "E-W"))))
        assertEquals(null, error(withWatch(home.watch.copy(routeShortName = ""))))
        assertEquals(ConfigError.Route, error(withWatch(home.watch.copy(routeShortName = "20 "))))
    }

    @Test fun `walk time, radius and coordinates`() {
        assertEquals(ConfigError.WalkMin, error(Config(places = listOf(home.copy(walkMin = 31)))))
        assertEquals(ConfigError.Radius, error(Config(places = listOf(home.copy(radiusM = 99)))))
        assertEquals(ConfigError.Coordinates, error(Config(places = listOf(home.copy(lat = -36.8)))))
        assertEquals(ConfigError.Coordinates, error(Config(places = listOf(home.copy(lat = 91.0, lon = 174.7)))))
        assertEquals(null, error(Config(places = listOf(home.copy(lat = -36.8, lon = 174.7)))))
    }

    @Test fun `the cache key changes with anything that changes the departures`() {
        assertNotEquals(home.watch.key, home.watch.copy(towardStopCode = "133").key)
        assertNotEquals(home.watch.key, home.watch.copy(routeShortName = "").key)
        assertEquals(home.watch.key, home.watch.copy(label = "renamed").key)
    }
}
