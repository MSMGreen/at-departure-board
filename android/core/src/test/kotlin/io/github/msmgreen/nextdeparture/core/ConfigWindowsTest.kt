package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ConfigWindowsTest {
    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val work = Place("Work", WatchConfig("home", "1060", "20", "8213"), walkMin = 2)
    private val morning = CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0)
    private val evening = CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1)
    private val ok = Config(places = listOf(home, work), windows = listOf(morning, evening))

    private fun error(vararg w: CommuteWindow) = ConfigCodec.validate(ok.copy(windows = w.toList()))

    @Test fun `round-trips with the spec's field names`() {
        val text = ConfigCodec.toJson(ok)
        for (k in listOf("\"windows\"", "\"days\":31", "\"start\":\"07:30\"", "\"end\":\"08:30\"", "\"default_place\":1")) {
            assertTrue(k in text, "$k in $text")
        }
        assertEquals(ConfigResult.Ok(ok), ConfigCodec.parse(text))
    }

    @Test fun `a phase 1 config reads back with no windows`() {
        val text = """{"v":1,"places":[{"name":"Home","watch":{"label":"","stop_code":"8213","toward_stop_code":"1060"}}]}"""
        assertEquals(emptyList(), (ConfigCodec.parse(text) as ConfigResult.Ok).config.windows)
    }

    @Test fun `weekdays by default, Monday in bit 0`() {
        assertEquals(0b0011111, morning.days)
        assertEquals(WEEKDAYS, morning.days)
    }

    @Test fun `at most four windows`() {
        assertEquals(null, error(*Array(4) { morning }))
        assertEquals(ConfigError.TooManyWindows, error(*Array(5) { morning }))
    }

    @Test fun `a window has at least one day and no more than seven`() {
        assertEquals(ConfigError.WindowDays, error(morning.copy(days = 0)))
        assertEquals(ConfigError.WindowDays, error(morning.copy(days = 0b10000000)))
        assertEquals(null, error(morning.copy(days = 0b1111111)))
    }

    @Test fun `times are HH colon MM, and the end is later the same day`() {
        for (bad in listOf("7:30", "07.30", "24:00", "07:60", "", "0730 ", "٠٧:٣٠")) {
            assertEquals(ConfigError.WindowTime, error(morning.copy(start = bad)), bad)
        }
        assertEquals(ConfigError.WindowTime, error(morning.copy(start = "08:30", end = "08:30")))
        assertEquals(ConfigError.WindowTime, error(morning.copy(start = "23:00", end = "01:00"))) // no crossing midnight
        assertEquals(null, error(morning.copy(start = "00:00", end = "23:59")))
    }

    @Test fun `removing a place keeps each window on the same place, or the first`() {
        // Places Home, Work, Gym; Work (1) is removed.
        assertEquals(0, placeAfterRemoval(0, removed = 1))
        assertEquals(0, placeAfterRemoval(1, removed = 1)) // its own place is gone
        assertEquals(1, placeAfterRemoval(2, removed = 1)) // Gym moves down one
        assertEquals(0, placeAfterRemoval(0, removed = 0))
    }

    @Test fun `a window's place is one of the places`() {
        assertEquals(ConfigError.WindowPlace, error(morning.copy(defaultPlace = 2)))
        assertEquals(ConfigError.WindowPlace, error(morning.copy(defaultPlace = -1)))
    }
}
