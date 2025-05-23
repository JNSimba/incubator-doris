// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
// This file is copied from
// https://github.com/ClickHouse/ClickHouse/blob/master/src/Functions/DateTimeTransforms.h
// and modified by Doris

#pragma once

#include "common/status.h"
#include "runtime/runtime_state.h"
#include "udf/udf.h"
#include "util/binary_cast.hpp"
#include "vec/columns/column_nullable.h"
#include "vec/columns/column_string.h"
#include "vec/columns/column_vector.h"
#include "vec/core/block.h"
#include "vec/core/column_numbers.h"
#include "vec/core/types.h"
#include "vec/data_types/data_type_date_time.h"
#include "vec/data_types/data_type_string.h"
#include "vec/functions/date_format_type.h"
#include "vec/runtime/vdatetime_value.h"
#include "vec/utils/util.hpp"

namespace doris::vectorized {
#include "common/compile_check_begin.h"

#define TIME_FUNCTION_IMPL(CLASS, UNIT, FUNCTION)                                   \
    template <typename NativeType>                                                  \
    struct CLASS {                                                                  \
        using OpArgType = NativeType;                                               \
        static constexpr auto name = #UNIT;                                         \
                                                                                    \
        static inline auto execute(const NativeType& t) {                           \
            const auto& date_time_value = (typename DateTraits<NativeType>::T&)(t); \
            return date_time_value.FUNCTION;                                        \
        }                                                                           \
                                                                                    \
        static DataTypes get_variadic_argument_types() {                            \
            return {std::make_shared<typename DateTraits<NativeType>::DateType>()}; \
        }                                                                           \
    }

#define TO_TIME_FUNCTION(CLASS, UNIT) TIME_FUNCTION_IMPL(CLASS, UNIT, UNIT())

TO_TIME_FUNCTION(ToYearImpl, year);
TO_TIME_FUNCTION(ToYearOfWeekImpl, year_of_week);
TO_TIME_FUNCTION(ToQuarterImpl, quarter);
TO_TIME_FUNCTION(ToMonthImpl, month);
TO_TIME_FUNCTION(ToDayImpl, day);
TO_TIME_FUNCTION(ToHourImpl, hour);
TO_TIME_FUNCTION(ToMinuteImpl, minute);
TO_TIME_FUNCTION(ToSecondImpl, second);
TO_TIME_FUNCTION(ToMicroSecondImpl, microsecond);

TIME_FUNCTION_IMPL(WeekOfYearImpl, weekofyear, week(mysql_week_mode(3)));
TIME_FUNCTION_IMPL(DayOfYearImpl, dayofyear, day_of_year());
TIME_FUNCTION_IMPL(DayOfMonthImpl, dayofmonth, day());
TIME_FUNCTION_IMPL(DayOfWeekImpl, dayofweek, day_of_week());
TIME_FUNCTION_IMPL(WeekDayImpl, weekday, weekday());
// TODO: the method should be always not nullable
TIME_FUNCTION_IMPL(ToDaysImpl, to_days, daynr());

#define TIME_FUNCTION_ONE_ARG_IMPL(CLASS, UNIT, FUNCTION)                        \
    template <typename ArgType>                                                  \
    struct CLASS {                                                               \
        using OpArgType = ArgType;                                               \
        static constexpr auto name = #UNIT;                                      \
                                                                                 \
        static inline auto execute(const ArgType& t) {                           \
            const auto& date_time_value = (typename DateTraits<ArgType>::T&)(t); \
            return date_time_value.FUNCTION;                                     \
        }                                                                        \
                                                                                 \
        static DataTypes get_variadic_argument_types() {                         \
            return {std::make_shared<typename DateTraits<ArgType>::DateType>()}; \
        }                                                                        \
    }

TIME_FUNCTION_ONE_ARG_IMPL(ToWeekOneArgImpl, week, week(mysql_week_mode(0)));
TIME_FUNCTION_ONE_ARG_IMPL(ToYearWeekOneArgImpl, yearweek, year_week(mysql_week_mode(0)));

template <typename ArgType>
struct ToDateImpl {
    using OpArgType = ArgType;
    using T = typename DateTraits<ArgType>::T;
    static constexpr auto name = "to_date";

    static inline auto execute(const ArgType& t) {
        auto dt = binary_cast<ArgType, T>(t);
        if constexpr (std::is_same_v<T, DateV2Value<DateV2ValueType>>) {
            return binary_cast<T, ArgType>(dt);
        } else if constexpr (std::is_same_v<T, VecDateTimeValue>) {
            dt.cast_to_date();
            return binary_cast<T, ArgType>(dt);
        } else {
            return (UInt32)(binary_cast<T, ArgType>(dt) >> TIME_PART_LENGTH);
        }
    }

    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename DateTraits<ArgType>::DateType>()};
    }
};

template <typename ArgType>
struct DateImpl : public ToDateImpl<ArgType> {
    static constexpr auto name = "date";
};

// TODO: This function look like no need do indeed copy here, we should optimize
// this function
template <typename ArgType>
struct TimeStampImpl {
    using OpArgType = ArgType;
    static constexpr auto name = "timestamp";

    static inline auto execute(const OpArgType& t) { return t; }

    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename DateTraits<ArgType>::DateType>()};
    }
};

template <typename ArgType>
struct DayNameImpl {
    using OpArgType = ArgType;
    static constexpr auto name = "dayname";
    static constexpr auto max_size = MAX_DAY_NAME_LEN;

    static inline auto execute(const typename DateTraits<ArgType>::T& dt,
                               ColumnString::Chars& res_data, size_t& offset) {
        const auto* day_name = dt.day_name();
        if (day_name != nullptr) {
            auto len = strlen(day_name);
            memcpy(&res_data[offset], day_name, len);
            offset += len;
        }
        return offset;
    }

    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename DateTraits<ArgType>::DateType>()};
    }
};

template <typename ArgType>
struct ToIso8601Impl {
    using OpArgType = ArgType;
    static constexpr auto name = "to_iso8601";
    static constexpr auto max_size = std::is_same_v<ArgType, UInt32> ? 10 : 26;

    static inline auto execute(const typename DateTraits<ArgType>::T& dt,
                               ColumnString::Chars& res_data, size_t& offset) {
        auto length = dt.to_buffer((char*)res_data.data() + offset,
                                   std::is_same_v<ArgType, UInt32> ? -1 : 6);
        if (std::is_same_v<ArgType, UInt64>) {
            res_data[offset + 10] = 'T';
        }

        offset += length;
        return offset;
    }

    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename DateTraits<ArgType>::DateType>()};
    }
};

template <typename ArgType>
struct MonthNameImpl {
    using OpArgType = ArgType;
    static constexpr auto name = "monthname";
    static constexpr auto max_size = MAX_MONTH_NAME_LEN;

    static inline auto execute(const typename DateTraits<ArgType>::T& dt,
                               ColumnString::Chars& res_data, size_t& offset) {
        const auto* month_name = dt.month_name();
        if (month_name != nullptr) {
            auto len = strlen(month_name);
            memcpy(&res_data[offset], month_name, len);
            offset += len;
        }
        return offset;
    }

    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename DateTraits<ArgType>::DateType>()};
    }
};

template <typename DateType, typename ArgType>
struct DateFormatImpl {
    using FromType = ArgType;

    static constexpr auto name = "date_format";

    template <typename Impl>
    static inline bool execute(const FromType& t, StringRef format, ColumnString::Chars& res_data,
                               size_t& offset, const cctz::time_zone& time_zone) {
        if constexpr (std::is_same_v<Impl, time_format_type::NoneImpl>) {
            // Handle non-special formats.
            const auto& dt = (DateType&)t;
            char buf[100 + SAFE_FORMAT_STRING_MARGIN];
            if (!dt.to_format_string_conservative(format.data, format.size, buf,
                                                  100 + SAFE_FORMAT_STRING_MARGIN)) {
                return true;
            }

            auto len = strlen(buf);
            res_data.insert(buf, buf + len);
            offset += len;
            return false;
        } else {
            const auto& dt = (DateType&)t;

            if (!dt.is_valid_date()) {
                return true;
            }

            // No buffer is needed here because these specially optimized formats have fixed lengths,
            // and sufficient memory has already been reserved.
            auto len = Impl::date_to_str(dt, (char*)res_data.data() + offset);
            offset += len;

            return false;
        }
    }

    static DataTypes get_variadic_argument_types() {
        return std::vector<DataTypePtr> {std::make_shared<typename DateTraits<ArgType>::DateType>(),
                                         std::make_shared<vectorized::DataTypeString>()};
    }
};

template <typename DateType>
struct FromUnixTimeImpl {
    using FromType = Int64;
    // https://dev.mysql.com/doc/refman/8.0/en/date-and-time-functions.html#function_from-unixtime
    // Keep consistent with MySQL
    static const int64_t TIMESTAMP_VALID_MAX = 32536771199;
    static constexpr auto name = "from_unixtime";

    template <typename Impl>
    static inline bool execute(const FromType& val, StringRef format, ColumnString::Chars& res_data,
                               size_t& offset, const cctz::time_zone& time_zone) {
        if constexpr (std::is_same_v<Impl, time_format_type::NoneImpl>) {
            DateType dt;
            if (val < 0 || val > TIMESTAMP_VALID_MAX) {
                return true;
            }
            dt.from_unixtime(val, time_zone);

            char buf[100 + SAFE_FORMAT_STRING_MARGIN];
            if (!dt.to_format_string_conservative(format.data, format.size, buf,
                                                  100 + SAFE_FORMAT_STRING_MARGIN)) {
                return true;
            }

            auto len = strlen(buf);
            res_data.insert(buf, buf + len);
            offset += len;
            return false;

        } else {
            DateType dt;
            if (val < 0 || val > TIMESTAMP_VALID_MAX) {
                return true;
            }
            dt.from_unixtime(val, time_zone);

            if (!dt.is_valid_date()) {
                return true;
            }

            // No buffer is needed here because these specially optimized formats have fixed lengths,
            // and sufficient memory has already been reserved.
            auto len = Impl::date_to_str(dt, (char*)res_data.data() + offset);
            offset += len;

            return false;
        }
    }
};

template <typename Transform>
struct TransformerToStringOneArgument {
    static void vector(FunctionContext* context,
                       const PaddedPODArray<typename Transform::OpArgType>& ts,
                       ColumnString::Chars& res_data, ColumnString::Offsets& res_offsets,
                       NullMap& null_map) {
        const auto len = ts.size();
        res_data.resize(len * Transform::max_size);
        res_offsets.resize(len);
        null_map.resize(len);

        size_t offset = 0;
        for (int i = 0; i < len; ++i) {
            const auto& t = ts[i];
            const auto& date_time_value =
                    reinterpret_cast<const typename DateTraits<typename Transform::OpArgType>::T&>(
                            t);
            res_offsets[i] =
                    cast_set<UInt32>(Transform::execute(date_time_value, res_data, offset));
            null_map[i] = !date_time_value.is_valid_date();
        }
        res_data.resize(res_offsets[res_offsets.size() - 1]);
    }

    static void vector(FunctionContext* context,
                       const PaddedPODArray<typename Transform::OpArgType>& ts,
                       ColumnString::Chars& res_data, ColumnString::Offsets& res_offsets) {
        const auto len = ts.size();
        res_data.resize(len * Transform::max_size);
        res_offsets.resize(len);

        size_t offset = 0;
        for (int i = 0; i < len; ++i) {
            const auto& t = ts[i];
            const auto& date_time_value =
                    reinterpret_cast<const typename DateTraits<typename Transform::OpArgType>::T&>(
                            t);
            res_offsets[i] =
                    cast_set<UInt32>(Transform::execute(date_time_value, res_data, offset));
            DCHECK(date_time_value.is_valid_date());
        }
        res_data.resize(res_offsets[res_offsets.size() - 1]);
    }
};

template <typename FromType, typename ToType, typename Transform>
struct Transformer {
    static void vector(const PaddedPODArray<FromType>& vec_from, PaddedPODArray<ToType>& vec_to,
                       NullMap& null_map) {
        size_t size = vec_from.size();
        vec_to.resize(size);
        null_map.resize(size);

        for (size_t i = 0; i < size; ++i) {
            // The transform result maybe an int, int32, but the result maybe short
            // for example, year function. It is only a short.
            auto res = Transform::execute(vec_from[i]);
            using RESULT_TYPE = std::decay_t<decltype(res)>;
            vec_to[i] = cast_set<ToType, RESULT_TYPE, false>(res);
        }

        for (size_t i = 0; i < size; ++i) {
            null_map[i] = !((typename DateTraits<typename Transform::OpArgType>::T&)(vec_from[i]))
                                   .is_valid_date();
        }
    }

    static void vector(const PaddedPODArray<FromType>& vec_from, PaddedPODArray<ToType>& vec_to) {
        size_t size = vec_from.size();
        vec_to.resize(size);

        for (size_t i = 0; i < size; ++i) {
            auto res = Transform::execute(vec_from[i]);
            using RESULT_TYPE = std::decay_t<decltype(res)>;
            vec_to[i] = cast_set<ToType, RESULT_TYPE, false>(res);
            DCHECK(((typename DateTraits<typename Transform::OpArgType>::T&)(vec_from[i]))
                           .is_valid_date());
        }
    }
};

template <typename FromType, typename ToType, template <typename> typename Impl>
struct TransformerYear {
    static void vector(const PaddedPODArray<FromType>& vec_from, PaddedPODArray<ToType>& vec_to,
                       NullMap& null_map) {
        size_t size = vec_from.size();
        vec_to.resize(size);
        null_map.resize(size);

        auto* __restrict to_ptr = vec_to.data();
        auto* __restrict from_ptr = vec_from.data();
        auto* __restrict null_map_ptr = null_map.data();

        for (size_t i = 0; i < size; ++i) {
            to_ptr[i] = Impl<FromType>::execute(from_ptr[i]);
        }

        for (size_t i = 0; i < size; ++i) {
            null_map_ptr[i] = to_ptr[i] > MAX_YEAR;
        }
    }

    static void vector(const PaddedPODArray<FromType>& vec_from, PaddedPODArray<ToType>& vec_to) {
        size_t size = vec_from.size();
        vec_to.resize(size);

        auto* __restrict to_ptr = vec_to.data();
        auto* __restrict from_ptr = vec_from.data();

        for (size_t i = 0; i < size; ++i) {
            to_ptr[i] = Impl<FromType>::execute(from_ptr[i]);
        }
    }
};

template <typename FromType, typename ToType>
struct Transformer<FromType, ToType, ToYearImpl<FromType>>
        : public TransformerYear<FromType, ToType, ToYearImpl> {};

template <typename FromType, typename ToType>
struct Transformer<FromType, ToType, ToYearOfWeekImpl<FromType>>
        : public TransformerYear<FromType, ToType, ToYearOfWeekImpl> {};

template <typename FromType, typename ToType, typename Transform>
struct DateTimeTransformImpl {
    static Status execute(Block& block, const ColumnNumbers& arguments, uint32_t result,
                          size_t input_rows_count) {
        using Op = Transformer<FromType, ToType, Transform>;

        const auto is_nullable = block.get_by_position(result).type->is_nullable();

        const ColumnPtr source_col = remove_nullable(block.get_by_position(arguments[0]).column);
        if (const auto* sources = check_and_get_column<ColumnVector<FromType>>(source_col.get())) {
            auto col_to = ColumnVector<ToType>::create();
            if (is_nullable) {
                auto null_map = ColumnVector<UInt8>::create(input_rows_count);
                Op::vector(sources->get_data(), col_to->get_data(), null_map->get_data());
                if (const auto* nullable_col = check_and_get_column<ColumnNullable>(
                            block.get_by_position(arguments[0]).column.get())) {
                    NullMap& result_null_map = assert_cast<ColumnUInt8&>(*null_map).get_data();
                    const NullMap& src_null_map =
                            assert_cast<const ColumnUInt8&>(nullable_col->get_null_map_column())
                                    .get_data();

                    VectorizedUtils::update_null_map(result_null_map, src_null_map);
                }
                block.replace_by_position(
                        result, ColumnNullable::create(std::move(col_to), std::move(null_map)));
            } else {
                Op::vector(sources->get_data(), col_to->get_data());
                block.replace_by_position(result, std::move(col_to));
            }
        } else {
            return Status::RuntimeError("Illegal column {} of first argument of function {}",
                                        block.get_by_position(arguments[0]).column->get_name(),
                                        Transform::name);
        }
        return Status::OK();
    }
};

#include "common/compile_check_end.h"
} // namespace doris::vectorized
