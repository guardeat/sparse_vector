#pragma once

#include <iterator>
#include <map>
#include <bitset>
#include <bit>
#include <concepts>

namespace byte {

	template<typename SparseVector_, bool IS_CONST_>
	class sparse_vector_iterator {
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = typename SparseVector_::value_type;
		using reference = std::conditional_t<IS_CONST_, const value_type&, value_type&>;
		using pointer = std::conditional_t<IS_CONST_, const value_type*, value_type*>;

		using const_reference = const value_type&;
		using const_pointer = const value_type*;

		using map_type =
			std::conditional_t<
			IS_CONST_,
			const typename SparseVector_::map_type,
			typename SparseVector_::map_type>;
		using map_ptr = std::conditional_t<IS_CONST_, const map_type*, map_type*>;

		using bitset = typename map_type::mapped_type;

		inline static constexpr size_t CHUNK_SIZE{ 64 };

	private:
		pointer elements_{ nullptr };
		size_t capacity_{ 0 };
		map_ptr control_{ nullptr };
		size_t index_{ 0 };
		bitset cache_{};

	public:
		sparse_vector_iterator() = default;

		sparse_vector_iterator(pointer elements, size_t capacity, map_type& control, size_t start) :
			elements_(elements), capacity_(capacity), control_(&control), index_{ start } {
			auto it{ control_->find(start / CHUNK_SIZE) };
			if (it != control_->end()) {
				cache_ = it->second;
				if (!cache_.test(start % CHUNK_SIZE)) {
					next_index();
				}
			}
			else {
				cache_.set();
			}
		}

		sparse_vector_iterator(const sparse_vector_iterator&) = default;

		sparse_vector_iterator& operator=(const sparse_vector_iterator&) = default;

		reference operator*() const {
			return elements_[index_];
		}

		pointer operator->() const {
			return elements_ + index_;
		}

		sparse_vector_iterator& operator++() {
			next_index();
			return *this;
		}

		sparse_vector_iterator operator++(int) {
			auto tmp{ *this };
			next_index();
			return tmp;
		}

		auto operator<=>(const sparse_vector_iterator& other) const {
			if (elements_ != other.elements_)
				return elements_ <=> other.elements_;
			return index_ <=> other.index_;
		}

		bool operator==(const sparse_vector_iterator& other) const {
			return elements_ == other.elements_ && index_ == other.index_;
		}

		size_t index() const {
			return index_;
		}

	private:
		void next_index() {
			++index_;

			size_t bit_index{ index_ % CHUNK_SIZE };
			size_t chunk_index{ index_ / CHUNK_SIZE };

			if (bit_index == 0) {
				auto it{ control_->find(chunk_index) };
				if (it == control_->end()) {
					cache_.set();
				}
				else {
					cache_ = it->second;
				}
			}

			size_t mask{ cache_.to_ullong() & (~0ULL << bit_index) };
			if (mask) {
				index_ = chunk_index * CHUNK_SIZE + static_cast<size_t>(std::countr_zero(mask));
				return;
			}

			size_t total_chunks{ (capacity_ + CHUNK_SIZE - 1) / CHUNK_SIZE };
			for (++chunk_index; chunk_index < total_chunks; ++chunk_index) {
				auto it{ control_->find(chunk_index) };
				if (it == control_->end()) {
					cache_.set();
				}
				else {
					cache_ = it->second;
				}

				uint64_t val{ cache_.to_ullong() };
				if (val) {
					index_ = chunk_index * CHUNK_SIZE + static_cast<size_t>(std::countr_zero(val));
					return;
				}
			}

			index_ = capacity_;
		}

	};
    

    template<typename Type_>
    concept SparseVectorValue = std::move_constructible<Type_> && std::destructible<Type_>;


	template<SparseVectorValue Type_, typename Allocator_ = std::allocator<Type_>>
	class sparse_vector {
	public:
		using value_type = Type_;
		using allocator_type = Allocator_;
		using size_type = size_t;
		using reference = value_type&;
		using const_reference = const value_type&;
		using pointer = typename std::allocator_traits<Allocator_>::pointer;
		using const_pointer = typename std::allocator_traits<Allocator_>::const_pointer;

		using iterator = sparse_vector_iterator<sparse_vector, false>;
		using const_iterator = sparse_vector_iterator<sparse_vector, true>;

		inline static constexpr size_t CHUNK_SIZE{ 64 };
		using map_type = std::map<size_t, std::bitset<CHUNK_SIZE>>;

	private:
		map_type control_;
		pointer elements_;
		size_t size_;
		size_t capacity_;
		Allocator_ alloc_;

		using allocator_traits = std::allocator_traits<Allocator_>;

	public:
		sparse_vector()
			:size_{ 0 }, capacity_{ CHUNK_SIZE } {
			elements_ = allocator_traits::allocate(alloc_, capacity_);
			control_.emplace(0, 0);
		}

		~sparse_vector() {
			if constexpr (!std::is_trivially_destructible_v<value_type>) {
				for (auto it{ begin() }; it.index() < capacity_; ++it) {
					allocator_traits::destroy(alloc_, elements_ + it.index());
				}
			}

			allocator_traits::deallocate(alloc_, elements_, capacity_);
		}

		size_t push(const_reference value) {
			return emplace(value);
		}

		size_t push(value_type&& value) {
			return emplace(std::move(value));
		}

		template<typename... Args>
		size_t emplace(Args&&... args) {
			if (size_ == capacity_) {
				reserve(capacity_ * 2);
			}
			size_t index{ next_index() };

			allocator_traits::construct(alloc_, elements_ + index, std::forward<Args>(args)...);

			++size_;

			return index;
		}

		reference at(size_t index) {
			return elements_[index];
		}

		const_reference at(size_t index) const {
			return elements_[index];
		}

		reference operator[](size_t index) {
			return elements_[index];
		}

		const_reference operator[](size_t index) const {
			return elements_[index];
		}

		void erase(size_t index) {
			size_t chunk_index{ index / CHUNK_SIZE };
			size_t bit_index{ index % CHUNK_SIZE };

			auto it{ control_.find(chunk_index) };

			if (it != control_.end()) {
				it->second.reset(bit_index);
			}
			else {
				std::bitset<CHUNK_SIZE> new_chunk;
				new_chunk.set();
				new_chunk.set(bit_index, false);
				control_.emplace(chunk_index, new_chunk);
			}

			if constexpr (!std::is_trivially_destructible_v<value_type>) {
				allocator_traits::destroy(alloc_, elements_ + index);
			}

			--size_;
		}

		iterator begin() {
			return iterator(elements_, capacity_, control_, 0);
		}

		iterator end() {
			return iterator(elements_, capacity_, control_, capacity_);
		}

		const_iterator begin() const {
			return const_iterator(elements_, capacity_, control_, 0);
		}

		const_iterator end() const {
			return const_iterator(elements_, capacity_, control_, capacity_);
		}

		size_t size() const {
			return size_;
		}

		void clear() {
			allocator_traits::deallocate(alloc_, elements_, capacity_);
			capacity_ = CHUNK_SIZE;
			elements_ = allocator_traits::allocate(alloc_, capacity_);
			size_ = 0;
			control_.clear();
			control_.emplace(0, 0);
		}

		void reserve(size_t new_capacity) {
			if (new_capacity <= capacity_) {
				return;
			}

			if (new_capacity % CHUNK_SIZE != 0) {
				new_capacity += CHUNK_SIZE - (new_capacity % CHUNK_SIZE);
			}

			pointer new_elements{ allocator_traits::allocate(alloc_, new_capacity) };

			for (auto it{ begin() }; it.index() < capacity_; ++it) {
				allocator_traits::construct(
					alloc_,
					new_elements + it.index(),
					std::move(elements_[it.index()]));
				if constexpr (!std::is_trivially_destructible_v<value_type>) {
					allocator_traits::destroy(alloc_, elements_ + it.index());
				}
			}

			allocator_traits::deallocate(alloc_, elements_, capacity_);

			size_t old_chunk_count{ capacity_ / CHUNK_SIZE };
			size_t new_chunk_count{ new_capacity / CHUNK_SIZE };

			elements_ = new_elements;
			capacity_ = new_capacity;

			for (size_t chunk_index{ old_chunk_count }; chunk_index < new_chunk_count; ++chunk_index) {
				control_.emplace(chunk_index, 0);
			}
		}

	private:
		size_t next_index() {
			auto it{ control_.begin() };

			size_t bit_index{ static_cast<size_t>(std::countr_zero(~it->second.to_ullong())) };
			it->second.set(bit_index);

			size_t chunk_index{ it->first };

			if (it->second.all()) {
				control_.erase(it);
			}

			return chunk_index * CHUNK_SIZE + bit_index;
		}
	};

}
