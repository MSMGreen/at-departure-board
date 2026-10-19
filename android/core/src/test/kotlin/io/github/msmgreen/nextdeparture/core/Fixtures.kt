package io.github.msmgreen.nextdeparture.core

import java.io.File

// The board's captured AT responses in test/fixtures/, shared with the C++
// suites. The path comes from core/build.gradle.kts.
fun fixture(path: String): String {
    val root = System.getProperty("fixtures") ?: error("run the tests through Gradle")
    return File(root, path).readText()
}

// Kingsland, 2026-09-13, from the post-CRL fixtures.
const val T_15_00 = 1789268400L // 2026-09-13 15:00:00 NZST
