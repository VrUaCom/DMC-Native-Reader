package com.vruacom.s26scannerlab.probe

import android.graphics.Rect
import android.graphics.RectF
import android.util.Range
import android.util.Rational
import android.util.Size
import android.util.SizeF
import org.json.JSONArray
import org.json.JSONObject

object JsonUtil {
    fun value(input: Any?): Any {
        if (input == null) return JSONObject.NULL
        return when (input) {
            is JSONObject, is JSONArray, is String, is Boolean -> input
            is Float -> if (input.isFinite()) input.toDouble() else input.toString()
            is Double -> if (input.isFinite()) input else input.toString()
            is Number -> input
            is IntArray -> JSONArray().also { a -> input.forEach { a.put(it) } }
            is LongArray -> JSONArray().also { a -> input.forEach { a.put(it) } }
            is FloatArray -> JSONArray().also { a -> input.forEach { a.put(value(it)) } }
            is DoubleArray -> JSONArray().also { a -> input.forEach { a.put(value(it)) } }
            is BooleanArray -> JSONArray().also { a -> input.forEach { a.put(it) } }
            is ByteArray -> JSONArray().also { a -> input.forEach { a.put(it.toInt() and 0xff) } }
            is ShortArray -> JSONArray().also { a -> input.forEach { a.put(it.toInt()) } }
            is Array<*> -> JSONArray().also { a -> input.forEach { a.put(value(it)) } }
            is Iterable<*> -> JSONArray().also { a -> input.forEach { a.put(value(it)) } }
            is Size -> JSONObject().put("width", input.width).put("height", input.height)
            is SizeF -> JSONObject().put("width", value(input.width)).put("height", value(input.height))
            is Rect -> JSONObject()
                .put("left", input.left).put("top", input.top)
                .put("right", input.right).put("bottom", input.bottom)
            is RectF -> JSONObject()
                .put("left", value(input.left)).put("top", value(input.top))
                .put("right", value(input.right)).put("bottom", value(input.bottom))
            is Range<*> -> JSONObject().put("lower", value(input.lower)).put("upper", value(input.upper))
            is Rational -> JSONObject().put("numerator", input.numerator).put("denominator", input.denominator)
            else -> input.toString()
        }
    }

    fun array(values: Iterable<*>): JSONArray =
        JSONArray().also { out -> values.forEach { out.put(value(it)) } }
}
