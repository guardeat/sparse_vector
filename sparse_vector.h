#pragma once

#include <map>
#include <bitset>
#include <vector>
#include <bit>
#include <compare>

namespace Byte {

	template<typename _SparseVector, bool _IsConst>
	class sparse_vector_iterator {
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = typename _SparseVector::value_type;
		using reference = std::conditional_t<_IsConst, const value_type&, value_type&>;
		using pointer = std::conditional_t<_IsConst, const value_type*, value_type*>;

		using const_reference = const value_type&;
		using const_pointer = const value_type*;

		using map_type = std::conditional_t<
			_IsConst,
			const typename _SparseVector::map_type,
			typename _SparseVector::map_type>;
		using bitset = typename map_type::mapped_type;

		inline static constexpr size_t CHUNK_SIZE{ 64 };

	private:
		pointer _elements{ nullptr };
		size_t _capacity{ 0 };
		map_type* _control{ nullptr };
		size_t _index{ 0 };
		bitset _cache{};

	public:
		sparse_vector_iterator() = default;

		sparse_vector_iterator(pointer elements, size_t capacity, map_type& control, size_t start) :
			_elements(elements), _capacity(capacity), _control(&control), _index{ start } {
			auto it{ _control->find(start / CHUNK_SIZE) };
			if (it != _control->end()) {
				_cache = it->second;
				if (!_cache.test(start % CHUNK_SIZE)) {
					next_index();
				}
			}
			else {
				_cache.set();
			}
		}

		sparse_vector_iterator(const sparse_vector_iterator&) = default;

		sparse_vector_iterator& operator=(const sparse_vector_iterator&) = default;

		reference operator*() const { return _elements[_index]; }
		pointer operator->() const { return _elements + _index; }

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
			if (_elements != other._elements)
				return _elements <=> other._elements;
			return _index <=> other._index;
		}

		bool operator==(const sparse_vector_iterator& other) const {
			return _elements == other._elements && _index == other._index;
		}

		size_t index() const {
			return _index;
		}

	private:
		void next_index() {
			++_index;

			size_t bit_index{ _index % CHUNK_SIZE };
			size_t chunk_index{ _index / CHUNK_SIZE };

			if (bit_index == 0) {
				auto it{ _control->find(chunk_index) };
				if (it == _control->end()) {
					return;
				}
				_cache = it->second;
			}

			size_t mask{ _cache.to_ullong() & (~0ULL << bit_index) };

			if (mask) {
				size_t leading_zeros{ static_cast<size_t>(std::countr_zero(mask)) };
				_index = chunk_index * CHUNK_SIZE + leading_zeros;
				return;
			}

			for (++chunk_index; chunk_index < (_capacity + CHUNK_SIZE - 1) / CHUNK_SIZE; ++chunk_index) {
				_index += CHUNK_SIZE - bit_index;
				auto it{ _control->find(chunk_index) };
				if (it == _control->end()) {
					return;
				}
				_cache = it->second;

				if (_cache.to_ullong()) {
					size_t leading_zeros{ static_cast<size_t>(std::countr_zero(_cache.to_ullong())) };
					_index = chunk_index * CHUNK_SIZE + leading_zeros;
					return;
				}
			}

			_index = _capacity;
		}
	};

	template<typename _Type, typename _Allocator = std::allocator<_Type>>
	class sparse_vector {
	public:
		using value_type = _Type;
		using allocator_type = _Allocator;
		using size_type = size_t;
		using reference = value_type&;
		using const_reference = const value_type&;
		using pointer = typename std::allocator_traits<_Allocator>::pointer;
		using const_pointer = typename std::allocator_traits<_Allocator>::const_pointer;

		using iterator = sparse_vector_iterator<sparse_vector, false>;
		using const_iterator = sparse_vector_iterator<sparse_vector, true>;

		inline static constexpr size_t CHUNK_SIZE{ 64 };
		using map_type = std::map<size_t, std::bitset<CHUNK_SIZE>>;

	private:
		map_type _control;
		pointer _elements;
		size_t _size;
		size_t _capacity;
		allocator_type _alloc;

		using allocator_traits = std::allocator_traits<_Allocator>;

	public:
		sparse_vector()
			:_size{ 0 }, _capacity{ CHUNK_SIZE } {
			_elements = allocator_traits::allocate(_alloc, _capacity);
			_control.emplace(0, 0);
		}

		~sparse_vector() {
			if constexpr (!std::is_trivially_destructible_v<value_type>) {
				for (auto it{ begin() }; it < end(); ++it) {
					allocator_traits::destroy(_alloc, _elements + it.index());
				}
			}

			allocator_traits::deallocate(_alloc, _elements, _capacity);
		}

		[[maybe_unused]] size_t push(const reference value) {
			return emplace(value);
		}

		[[maybe_unused]] size_t push(value_type&& value) {
			return emplace(std::move(value));
		}

		template<typename... Args>
		[[maybe_unused]] size_t emplace(Args&&... args) {
			if (_size == _capacity) {
				reserve(_capacity * 2);
			}
			size_t index{ next_index() };

			allocator_traits::construct(_alloc, _elements + index, std::forward<Args>(args)...);

			++_size;

			return index;
		}

		reference at(size_t index) {
			return _elements[index];
		}

		const reference at(size_t index) const {
			return _elements[index];
		}

		reference operator[](size_t index) {
			return _elements[index];
		}

		const reference operator[](size_t index) const {
			return _elements[index];
		}

		void erase(size_t index) {
			size_t chunk_index{ index / CHUNK_SIZE };
			size_t bit_index{ index % CHUNK_SIZE };

			auto it{ _control.find(chunk_index) };

			if (it != _control.end()) {
				it->second.reset(bit_index);
			}
			else {
				std::bitset<CHUNK_SIZE> new_chunk;
				new_chunk.set();
				new_chunk.set(bit_index, false);
				_control.emplace(chunk_index, new_chunk);
			}

			if constexpr (!std::is_trivially_destructible_v<value_type>) {
				allocator_traits::destroy(_alloc, _elements + index);
			}

			--_size;
		}

		iterator begin() {
			return iterator(_elements, _capacity, _control, 0);
		}

		iterator end() {
			return iterator(_elements, _capacity, _control, _capacity);
		}

		const_iterator begin() const {
			return const_iterator(_elements, _capacity, _control, 0);
		}

		const_iterator end() const {
			return const_iterator(_elements, _capacity, _control, _capacity);
		}

		size_t size() const {
			return _size;
		}

		void clear() {
			allocator_traits::deallocate(_alloc, _elements, _capacity);
			_capacity = CHUNK_SIZE;
			_elements = allocator_traits::allocate(_alloc, _capacity);
			_size = 0;
			_control.clear();
			_control.emplace(0, 0);
		}

		void reserve(size_t new_capacity) {
			if (new_capacity <= _capacity) {
				return;
			}

			if (new_capacity % CHUNK_SIZE != 0) {
				new_capacity += CHUNK_SIZE - (new_capacity % CHUNK_SIZE);
			}

			pointer new_elements{ allocator_traits::allocate(_alloc, new_capacity) };

			for (auto it{ begin() }; it < end(); ++it) {
				allocator_traits::construct(
					_alloc,
					new_elements + it.index(),
					std::move(_elements[it.index()]));
				if constexpr (!std::is_trivially_destructible_v<value_type>) {
					allocator_traits::destroy(_alloc, _elements + it.index());
				}
			}

			allocator_traits::deallocate(_alloc, _elements, _capacity);

			size_t old_chunk_count{ _capacity / CHUNK_SIZE };
			size_t new_chunk_count{ new_capacity / CHUNK_SIZE };

			_elements = new_elements;
			_capacity = new_capacity;

			for (size_t chunk_index{ old_chunk_count }; chunk_index < new_chunk_count; ++chunk_index) {
				_control.emplace(chunk_index, 0);
			}
		}

	private:
		size_t next_index() {
			auto it{ _control.begin() };

			size_t bit_index{
				static_cast<size_t>(CHUNK_SIZE - std::countl_zero(it->second.to_ullong())) };
			it->second.set(bit_index);

			size_t chunk_index{ it->first };

			if (it->second.all()) {
				_control.erase(it);
			}

			return  chunk_index * CHUNK_SIZE + bit_index;
		}
	};

}
