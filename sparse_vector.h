#pragma once

#include <bit>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace byte {

template <typename Type_>
concept SparseVectorValue = std::move_constructible<Type_> && std::destructible<Type_>;

/// @brief Index-stable slots with holes. insert/emplace reuse the lowest free index.
template <SparseVectorValue Type_, typename Alloc_ = std::allocator<Type_>>
class sparse_vector {
public:
  using value_type = Type_;
  using allocator_type = Alloc_;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = typename std::allocator_traits<Alloc_>::pointer;
  using const_pointer = typename std::allocator_traits<Alloc_>::const_pointer;

  static constexpr size_type chunk_size = 64;
  using map_type = std::map<size_type, std::bitset<chunk_size>>;

  template <bool Const_>
  class basic_iterator {
  public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = sparse_vector::difference_type;
    using value_type = sparse_vector::value_type;
    using pointer = std::conditional_t<Const_, sparse_vector::const_pointer, sparse_vector::pointer>;
    using reference = std::conditional_t<Const_, const_reference, sparse_vector::reference>;

  private:
    pointer data_{};
    const map_type* control_{};
    size_type capacity_{};
    size_type index_{};
    std::bitset<chunk_size> cache_{};

  public:
    basic_iterator() = default;

    basic_iterator(pointer data, const map_type& control, size_type capacity, size_type index)
        : data_{data}, control_{&control}, capacity_{capacity}, index_{index} {
      if (index_ >= capacity_) {
        index_ = capacity_;
        return;
      }
      load_cache(index_ / chunk_size);
      if (!cache_.test(index_ % chunk_size)) {
        advance();
      }
    }

    [[nodiscard]] operator basic_iterator<true>() const
      requires (!Const_)
    {
      return {data_, *control_, capacity_, index_};
    }

    [[nodiscard]] reference operator*() const {
      return data_[index_];
    }

    [[nodiscard]] pointer operator->() const {
      return data_ + index_;
    }

    basic_iterator& operator++() {
      advance();
      return *this;
    }

    basic_iterator operator++(int) {
      basic_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    [[nodiscard]] bool operator==(const basic_iterator& other) const {
      return data_ == other.data_ && index_ == other.index_;
    }

    [[nodiscard]] size_type index() const noexcept {
      return index_;
    }

  private:
    void load_cache(size_type chunk) {
      auto it = control_->find(chunk);
      if (it == control_->end()) {
        cache_.set();
      } else {
        cache_ = it->second;
      }
    }

    void advance() {
      ++index_;
      if (index_ >= capacity_) {
        index_ = capacity_;
        return;
      }

      size_type bit = index_ % chunk_size;
      size_type chunk = index_ / chunk_size;
      if (bit == 0) {
        load_cache(chunk);
      }

      const std::uint64_t bits = cache_.to_ullong() & (~0ULL << bit);
      if (bits != 0) {
        index_ = chunk * chunk_size + static_cast<size_type>(std::countr_zero(bits));
        return;
      }

      const size_type total = capacity_ / chunk_size;
      for (++chunk; chunk < total; ++chunk) {
        load_cache(chunk);
        const std::uint64_t word = cache_.to_ullong();
        if (word != 0) {
          index_ = chunk * chunk_size + static_cast<size_type>(std::countr_zero(word));
          return;
        }
      }
      index_ = capacity_;
    }
  };

  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

private:
  using AllocTraits_ = std::allocator_traits<Alloc_>;

  map_type control_{};
  pointer data_{};
  size_type size_{};
  size_type capacity_{};
  [[no_unique_address]] Alloc_ alloc_{};

public:
  sparse_vector() = default;

  explicit sparse_vector(const Alloc_& alloc) : alloc_(alloc) {}

  sparse_vector(const sparse_vector& other)
      : alloc_(AllocTraits_::select_on_container_copy_construction(other.alloc_)) {
    copy_occupied(other);
  }

  sparse_vector(sparse_vector&& other) noexcept
      : control_(std::move(other.control_)),
        data_(other.data_),
        size_(other.size_),
        capacity_(other.capacity_),
        alloc_(std::move(other.alloc_)) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  ~sparse_vector() {
    destroy_occupied();
    deallocate();
  }

  sparse_vector& operator=(const sparse_vector& other) {
    if (this != &other) {
      sparse_vector tmp(other);
      swap(tmp);
    }
    return *this;
  }

  sparse_vector& operator=(sparse_vector&& other) noexcept {
    if (this != &other) {
      destroy_occupied();
      deallocate();
      control_ = std::move(other.control_);
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      alloc_ = std::move(other.alloc_);
      other.data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
    }
    return *this;
  }

  [[nodiscard]] allocator_type get_allocator() const noexcept {
    return alloc_;
  }

  [[nodiscard]] iterator begin() noexcept {
    return {data_, control_, capacity_, 0};
  }

  [[nodiscard]] const_iterator begin() const noexcept {
    return {data_, control_, capacity_, 0};
  }

  [[nodiscard]] const_iterator cbegin() const noexcept {
    return begin();
  }

  [[nodiscard]] iterator end() noexcept {
    return {data_, control_, capacity_, capacity_};
  }

  [[nodiscard]] const_iterator end() const noexcept {
    return {data_, control_, capacity_, capacity_};
  }

  [[nodiscard]] const_iterator cend() const noexcept {
    return end();
  }

  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] size_type size() const noexcept {
    return size_;
  }

  [[nodiscard]] size_type capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] size_type max_size() const noexcept {
    return AllocTraits_::max_size(alloc_);
  }

  void reserve(size_type n) {
    n = ceil_chunk(n);
    if (n <= capacity_) {
      return;
    }

    pointer neu = AllocTraits_::allocate(alloc_, n);
    if (data_ != nullptr) {
      for (auto it = begin(); it != end(); ++it) {
        AllocTraits_::construct(alloc_, neu + it.index(), std::move(*it));
      }
      destroy_occupied();
      AllocTraits_::deallocate(alloc_, data_, capacity_);
    }

    const size_type old_chunks = capacity_ / chunk_size;
    data_ = neu;
    capacity_ = n;
    for (size_type chunk = old_chunks; chunk < capacity_ / chunk_size; ++chunk) {
      control_.emplace(chunk, 0);
    }
  }

  void shrink_to_fit() {
    if (empty()) {
      destroy_occupied();
      deallocate();
      size_ = 0;
      return;
    }

    size_type last = 0;
    for (auto it = begin(); it != end(); ++it) {
      last = it.index();
    }
    const size_type n = ceil_chunk(last + 1);
    if (n >= capacity_) {
      return;
    }

    pointer neu = AllocTraits_::allocate(alloc_, n);
    for (auto it = begin(); it != end(); ++it) {
      AllocTraits_::construct(alloc_, neu + it.index(), std::move(*it));
    }
    destroy_occupied();
    AllocTraits_::deallocate(alloc_, data_, capacity_);
    data_ = neu;
    capacity_ = n;
    control_.erase(control_.lower_bound(capacity_ / chunk_size), control_.end());
  }

  void clear() noexcept {
    destroy_occupied();
    reset_control_empty();
    size_ = 0;
  }

  template <typename... Args_>
  size_type emplace(Args_&&... args) {
    if (size_ == capacity_) {
      reserve(capacity_ == 0 ? chunk_size : capacity_ * 2);
    }
    const size_type index = claim_free();
    AllocTraits_::construct(alloc_, data_ + index, std::forward<Args_>(args)...);
    ++size_;
    return index;
  }

  size_type insert(const_reference value) {
    return emplace(value);
  }

  size_type insert(value_type&& value) {
    return emplace(std::move(value));
  }

  size_type erase(size_type index) {
    if (!contains(index)) {
      return 0;
    }
    AllocTraits_::destroy(alloc_, data_ + index);
    release(index);
    --size_;
    return 1;
  }

  iterator erase(const_iterator pos) {
    const size_type index = pos.index();
    ++pos;
    erase(index);
    return {data_, control_, capacity_, pos.index()};
  }

  [[nodiscard]] reference at(size_type index) {
    if (!contains(index)) {
      throw std::out_of_range("sparse_vector::at");
    }
    return data_[index];
  }

  [[nodiscard]] const_reference at(size_type index) const {
    if (!contains(index)) {
      throw std::out_of_range("sparse_vector::at");
    }
    return data_[index];
  }

  [[nodiscard]] reference operator[](size_type index) {
    return data_[index];
  }

  [[nodiscard]] const_reference operator[](size_type index) const {
    return data_[index];
  }

  [[nodiscard]] bool contains(size_type index) const noexcept {
    return index < capacity_ && occupied(index);
  }

  void swap(sparse_vector& other) noexcept {
    using std::swap;
    swap(control_, other.control_);
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
    swap(alloc_, other.alloc_);
  }

  friend void swap(sparse_vector& a, sparse_vector& b) noexcept {
    a.swap(b);
  }

private:
  [[nodiscard]] static size_type ceil_chunk(size_type n) noexcept {
    return (n + chunk_size - 1) / chunk_size * chunk_size;
  }

  [[nodiscard]] bool occupied(size_type index) const noexcept {
    const auto it = control_.find(index / chunk_size);
    if (it == control_.end()) {
      return true;
    }
    return it->second.test(index % chunk_size);
  }

  [[nodiscard]] size_type claim_free() {
    while (!control_.empty()) {
      auto it = control_.begin();
      const std::uint64_t holes = ~it->second.to_ullong();
      if (holes == 0) {
        control_.erase(it);
        continue;
      }
      const size_type bit = static_cast<size_type>(std::countr_zero(holes));
      const size_type chunk = it->first;
      it->second.set(bit);
      if (it->second.all()) {
        control_.erase(it);
      }
      return chunk * chunk_size + bit;
    }
    return capacity_;
  }

  void release(size_type index) {
    const size_type chunk = index / chunk_size;
    const size_type bit = index % chunk_size;
    auto it = control_.find(chunk);
    if (it != control_.end()) {
      it->second.reset(bit);
      return;
    }
    std::bitset<chunk_size> bits;
    bits.set();
    bits.reset(bit);
    control_.emplace(chunk, bits);
  }

  void occupy_at(size_type index) {
    const size_type chunk = index / chunk_size;
    const size_type bit = index % chunk_size;
    auto it = control_.find(chunk);
    it->second.set(bit);
    if (it->second.all()) {
      control_.erase(it);
    }
  }

  void reset_control_empty() {
    control_.clear();
    for (size_type chunk = 0; chunk < capacity_ / chunk_size; ++chunk) {
      control_.emplace(chunk, 0);
    }
  }

  void destroy_occupied() noexcept {
    if (data_ == nullptr) {
      return;
    }
    if constexpr (std::is_trivially_destructible_v<value_type>) {
      return;
    }
    for (auto it = begin(); it != end(); ++it) {
      AllocTraits_::destroy(alloc_, data_ + it.index());
    }
  }

  void deallocate() noexcept {
    if (data_ == nullptr) {
      return;
    }
    AllocTraits_::deallocate(alloc_, data_, capacity_);
    data_ = nullptr;
    capacity_ = 0;
    control_.clear();
  }

  void copy_occupied(const sparse_vector& other) {
    if (other.capacity_ == 0) {
      return;
    }
    data_ = AllocTraits_::allocate(alloc_, other.capacity_);
    capacity_ = other.capacity_;
    reset_control_empty();
    try {
      for (auto it = other.begin(); it != other.end(); ++it) {
        AllocTraits_::construct(alloc_, data_ + it.index(), *it);
        occupy_at(it.index());
        ++size_;
      }
    } catch (...) {
      destroy_occupied();
      deallocate();
      size_ = 0;
      throw;
    }
  }
};

} // namespace byte
