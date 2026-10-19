package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable

@Serializable
data class Rgb(val r: Int, val g: Int, val b: Int) {
    val argb: Int get() = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
}

// '97C93D' or '#97C93D'. Null for anything unusable - including #000000, which
// AT sends for HUIA and which would be invisible (lib/core/src/color.cpp).
fun parseHex(s: String?): Rgb? {
    val hex = s?.trim()?.trimStart('#') ?: return null
    if (hex.length != 6 || !hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
    val v = hex.toInt(16)
    if (v == 0) return null
    return Rgb(v shr 16 and 0xFF, v shr 8 and 0xFF, v and 0xFF)
}
