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

#pragma once

#include <memory>
#include <utility>

#include "arrow/compute/function.h"
#include "arrow/datum.h"
#include "arrow/result.h"
#include "arrow/type_fwd.h"

namespace arrow {
namespace compute {

class ExecContext;

/// \addtogroup compute-concrete-options
/// @{

class ARROW_EXPORT FilterOptions : public FunctionOptions {
 public:
  /// Configure the action taken when a slot of the selection mask is null
  enum NullSelectionBehavior {
    /// the corresponding filtered value will be removed in the output
    DROP,
    /// the corresponding filtered value will be null in the output
    EMIT_NULL,
  };

  explicit FilterOptions(NullSelectionBehavior null_selection = DROP);
  constexpr static char const kTypeName[] = "FilterOptions";
  static FilterOptions Defaults() { return FilterOptions(); }

  NullSelectionBehavior null_selection_behavior = DROP;
};

class ARROW_EXPORT TakeOptions : public FunctionOptions {
 public:
  explicit TakeOptions(bool boundscheck = true);
  constexpr static char const kTypeName[] = "TakeOptions";
  static TakeOptions BoundsCheck() { return TakeOptions(true); }
  static TakeOptions NoBoundsCheck() { return TakeOptions(false); }
  static TakeOptions Defaults() { return BoundsCheck(); }

  bool boundscheck = true;
};

/// \brief Options for the dictionary encode function
class ARROW_EXPORT DictionaryEncodeOptions : public FunctionOptions {
 public:
  /// Configure how null values will be encoded
  enum NullEncodingBehavior {
    /// the null value will be added to the dictionary with a proper index
    ENCODE,
    /// the null value will be masked in the indices array
    MASK
  };

  explicit DictionaryEncodeOptions(NullEncodingBehavior null_encoding = MASK);
  constexpr static char const kTypeName[] = "DictionaryEncodeOptions";
  static DictionaryEncodeOptions Defaults() { return DictionaryEncodeOptions(); }

  NullEncodingBehavior null_encoding_behavior = MASK;
};

enum class SortOrder {
  Ascending,
  Descending,
};

/// \brief One sort key for PartitionNthIndices (TODO) and SortIndices
class ARROW_EXPORT SortKey : public util::EqualityComparable<SortKey> {
 public:
  explicit SortKey(std::string name, SortOrder order = SortOrder::Ascending)
      : name(std::move(name)), order(order) {}

  using util::EqualityComparable<SortKey>::Equals;
  using util::EqualityComparable<SortKey>::operator==;
  using util::EqualityComparable<SortKey>::operator!=;
  bool Equals(const SortKey& other) const;
  std::string ToString() const;

  /// The name of the sort column.
  std::string name;
  /// How to order by this sort key.
  SortOrder order;
};

class ARROW_EXPORT ArraySortOptions : public FunctionOptions {
 public:
  explicit ArraySortOptions(SortOrder order = SortOrder::Ascending);
  constexpr static char const kTypeName[] = "ArraySortOptions";
  static ArraySortOptions Defaults() { return ArraySortOptions{}; }

  SortOrder order;
};

class ARROW_EXPORT SortOptions : public FunctionOptions {
 public:
  explicit SortOptions(std::vector<SortKey> sort_keys = {});
  constexpr static char const kTypeName[] = "SortOptions";
  static SortOptions Defaults() { return SortOptions{}; }

  std::vector<SortKey> sort_keys;
};

/// \brief Partitioning options for NthToIndices
class ARROW_EXPORT PartitionNthOptions : public FunctionOptions {
 public:
  explicit PartitionNthOptions(int64_t pivot);
  PartitionNthOptions() : PartitionNthOptions(0) {}
  constexpr static char const kTypeName[] = "PartitionNthOptions";

  /// The index into the equivalent sorted array of the partition pivot element.
  int64_t pivot;
};

/// @}

/// \brief Filter with a boolean selection filter
///
/// The output will be populated with values from the input at positions
/// where the selection filter is not 0. Nulls in the filter will be handled
/// based on options.null_selection_behavior.
///
/// For example given values = ["a", "b", "c", null, "e", "f"] and
/// filter = [0, 1, 1, 0, null, 1], the output will be
/// (null_selection_behavior == DROP)      = ["b", "c", "f"]
/// (null_selection_behavior == EMIT_NULL) = ["b", "c", null, "f"]
///
/// \param[in] values array to filter
/// \param[in] filter indicates which values should be filtered out
/// \param[in] options configures null_selection_behavior
/// \param[in] ctx the function execution context, optional
/// \return the resulting datum
ARROW_EXPORT
Result<Datum> Filter(const Datum& values, const Datum& filter,
                     const FilterOptions& options = FilterOptions::Defaults(),
                     ExecContext* ctx = NULLPTR);

namespace internal {

// These internal functions are implemented in kernels/vector_selection.cc

/// \brief Return the number of selected indices in the boolean filter
ARROW_EXPORT
int64_t GetFilterOutputSize(const ArrayData& filter,
                            FilterOptions::NullSelectionBehavior null_selection);

/// \brief Compute uint64 selection indices for use with Take given a boolean
/// filter
ARROW_EXPORT
Result<std::shared_ptr<ArrayData>> GetTakeIndices(
    const ArrayData& filter, FilterOptions::NullSelectionBehavior null_selection,
    MemoryPool* memory_pool = default_memory_pool());

}  // namespace internal

/// \brief ReplaceWithMask replaces each value in the array corresponding
/// to a true value in the mask with the next element from `replacements`.
///
/// \param[in] values Array input to replace
/// \param[in] mask Array or Scalar of Boolean mask values
/// \param[in] replacements The replacement values to draw from. There must
/// be as many replacement values as true values in the mask.
/// \param[in] ctx the function execution context, optional
///
/// \return the resulting datum
///
/// \since 5.0.0
/// \note API not yet finalized
ARROW_EXPORT
Result<Datum> ReplaceWithMask(const Datum& values, const Datum& mask,
                              const Datum& replacements, ExecContext* ctx = NULLPTR);

/// \brief Take from an array of values at indices in another array
///
/// The output array will be of the same type as the input values
/// array, with elements taken from the values array at the given
/// indices. If an index is null then the taken element will be null.
///
/// For example given values = ["a", "b", "c", null, "e", "f"] and
/// indices = [2, 1, null, 3], the output will be
/// = [values[2], values[1], null, values[3]]
/// = ["c", "b", null, null]
///
/// \param[in] values datum from which to take
/// \param[in] indices which values to take
/// \param[in] options options
/// \param[in] ctx the function execution context, optional
/// \return the resulting datum
ARROW_EXPORT
Result<Datum> Take(const Datum& values, const Datum& indices,
                   const TakeOptions& options = TakeOptions::Defaults(),
                   ExecContext* ctx = NULLPTR);

/// \brief Take with Array inputs and output
ARROW_EXPORT
Result<std::shared_ptr<Array>> Take(const Array& values, const Array& indices,
                                    const TakeOptions& options = TakeOptions::Defaults(),
                                    ExecContext* ctx = NULLPTR);

/// \brief Drop Null from an array of values
///
/// The output array will be of the same type as the input values
/// array, with elements taken from the values array without nulls.
///
/// For example given values = ["a", "b", "c", null, "e", "f"],
/// the output will be = ["a", "b", "c", "e", "f"]
///
/// \param[in] values datum from which to take
/// \param[in] ctx the function execution context, optional
/// \return the resulting datum
ARROW_EXPORT
Result<Datum> DropNull(const Datum& values, ExecContext* ctx = NULLPTR);

/// \brief DropNull with Array inputs and output
ARROW_EXPORT
Result<std::shared_ptr<Array>> DropNull(const Array& values, ExecContext* ctx = NULLPTR);

/// \brief Returns indices that partition an array around n-th
/// sorted element.
///
/// Find index of n-th(0 based) smallest value and perform indirect
/// partition of an array around that element. Output indices[0 ~ n-1]
/// holds values no greater than n-th element, and indices[n+1 ~ end]
/// holds values no less than n-th element. Elements in each partition
/// is not sorted. Nulls will be partitioned to the end of the output.
/// Output is not guaranteed to be stable.
///
/// \param[in] values array to be partitioned
/// \param[in] n pivot array around sorted n-th element
/// \param[in] ctx the function execution context, optional
/// \return offsets indices that would partition an array
ARROW_EXPORT
Result<std::shared_ptr<Array>> NthToIndices(const Array& values, int64_t n,
                                            ExecContext* ctx = NULLPTR);

/// \brief Returns the indices that would sort an array in the
/// specified order.
///
/// Perform an indirect sort of array. The output array will contain
/// indices that would sort an array, which would be the same length
/// as input. Nulls will be stably partitioned to the end of the output
/// regardless of order.
///
/// For example given array = [null, 1, 3.3, null, 2, 5.3] and order
/// = SortOrder::DESCENDING, the output will be [5, 2, 4, 1, 0,
/// 3].
///
/// \param[in] array array to sort
/// \param[in] order ascending or descending
/// \param[in] ctx the function execution context, optional
/// \return offsets indices that would sort an array
ARROW_EXPORT
Result<std::shared_ptr<Array>> SortIndices(const Array& array,
                                           SortOrder order = SortOrder::Ascending,
                                           ExecContext* ctx = NULLPTR);

/// \brief Returns the indices that would sort a chunked array in the
/// specified order.
///
/// Perform an indirect sort of chunked array. The output array will
/// contain indices that would sort a chunked array, which would be
/// the same length as input. Nulls will be stably partitioned to the
/// end of the output regardless of order.
///
/// For example given chunked_array = [[null, 1], [3.3], [null, 2,
/// 5.3]] and order = SortOrder::DESCENDING, the output will be [5, 2,
/// 4, 1, 0, 3].
///
/// \param[in] chunked_array chunked array to sort
/// \param[in] order ascending or descending
/// \param[in] ctx the function execution context, optional
/// \return offsets indices that would sort an array
ARROW_EXPORT
Result<std::shared_ptr<Array>> SortIndices(const ChunkedArray& chunked_array,
                                           SortOrder order = SortOrder::Ascending,
                                           ExecContext* ctx = NULLPTR);

/// \brief Returns the indices that would sort an input in the
/// specified order. Input is one of array, chunked array record batch
/// or table.
///
/// Perform an indirect sort of input. The output array will contain
/// indices that would sort an input, which would be the same length
/// as input. Nulls will be stably partitioned to the end of the
/// output regardless of order.
///
/// For example given input (table) = {
/// "column1": [[null,   1], [   3, null, 2, 1]],
/// "column2": [[   5], [3,   null, null, 5, 5]],
/// } and options = {
/// {"column1", SortOrder::Ascending},
/// {"column2", SortOrder::Descending},
/// }, the output will be [5, 1, 4, 2, 0, 3].
///
/// \param[in] datum array, chunked array, record batch or table to sort
/// \param[in] options options
/// \param[in] ctx the function execution context, optional
/// \return offsets indices that would sort a table
ARROW_EXPORT
Result<std::shared_ptr<Array>> SortIndices(const Datum& datum, const SortOptions& options,
                                           ExecContext* ctx = NULLPTR);

/// \brief Compute unique elements from an array-like object
///
/// Note if a null occurs in the input it will NOT be included in the output.
///
/// \param[in] datum array-like input
/// \param[in] ctx the function execution context, optional
/// \return result as Array
///
/// \since 1.0.0
/// \note API not yet finalized
ARROW_EXPORT
Result<std::shared_ptr<Array>> Unique(const Datum& datum, ExecContext* ctx = NULLPTR);

// Constants for accessing the output of ValueCounts
ARROW_EXPORT extern const char kValuesFieldName[];
ARROW_EXPORT extern const char kCountsFieldName[];
ARROW_EXPORT extern const int32_t kValuesFieldIndex;
ARROW_EXPORT extern const int32_t kCountsFieldIndex;

/// \brief Return counts of unique elements from an array-like object.
///
/// Note that the counts do not include counts for nulls in the array.  These can be
/// obtained separately from metadata.
///
/// For floating point arrays there is no attempt to normalize -0.0, 0.0 and NaN values
/// which can lead to unexpected results if the input Array has these values.
///
/// \param[in] value array-like input
/// \param[in] ctx the function execution context, optional
/// \return counts An array of  <input type "Values", int64_t "Counts"> structs.
///
/// \since 1.0.0
/// \note API not yet finalized
ARROW_EXPORT
Result<std::shared_ptr<StructArray>> ValueCounts(const Datum& value,
                                                 ExecContext* ctx = NULLPTR);

/// \brief Dictionary-encode values in an array-like object
///
/// Any nulls encountered in the dictionary will be handled according to the
/// specified null encoding behavior.
///
/// For example, given values ["a", "b", null, "a", null] the output will be
/// (null_encoding == ENCODE) Indices: [0, 1, 2, 0, 2] / Dict: ["a", "b", null]
/// (null_encoding == MASK)   Indices: [0, 1, null, 0, null] / Dict: ["a", "b"]
///
/// If the input is already dictionary encoded this function is a no-op unless
/// it needs to modify the null_encoding (TODO)
///
/// \param[in] data array-like input
/// \param[in] ctx the function execution context, optional
/// \param[in] options configures null encoding behavior
/// \return result with same shape and type as input
///
/// \since 1.0.0
/// \note API not yet finalized
ARROW_EXPORT
Result<Datum> DictionaryEncode(
    const Datum& data,
    const DictionaryEncodeOptions& options = DictionaryEncodeOptions::Defaults(),
    ExecContext* ctx = NULLPTR);

// ----------------------------------------------------------------------
// Deprecated functions

ARROW_DEPRECATED("Deprecated in 3.0.0. Use SortIndices()")
ARROW_EXPORT
Result<std::shared_ptr<Array>> SortToIndices(const Array& values,
                                             ExecContext* ctx = NULLPTR);

}  // namespace compute
}  // namespace arrow
