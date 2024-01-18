#ifndef BYTE_SPARCEVECTOR_H
#define BYTE_SPARCEVECTOR_H

#include <bitset>
#include <memory>
#include <vector>
#include <bit>
#include <limits>
#include <set>
#include <type_traits>

namespace Byte
{

	inline static constexpr size_t _BITSET_SIZE{ 64 };

	template<typename T>
	class sparse_vector_iterator
	{
	private:
		using bitset64 = std::bitset<_BITSET_SIZE>;
		using bitset_vector = std::conditional_t<std::is_const<T>::value, const std::vector<bitset64>, std::vector<bitset64>>;

	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = value_type&;

	private:
		pointer data;
		bitset_vector* bitsets_ptr;
		size_t _index;

	public:
		sparse_vector_iterator(T* data, size_t _index, bitset_vector* bitsets)
			:data{ data }, _index{ _index }, bitsets_ptr{ bitsets }
		{
			if (bitsets_ptr && !bitsets_ptr->at(_index / _BITSET_SIZE).test(_BITSET_SIZE - 1ULL - _index % _BITSET_SIZE))
			{
				++(*this);
			}
		}

		reference operator*()
		{
			return data[_index];
		}

		pointer operator->()
		{
			return data + _index;
		}

		sparse_vector_iterator& operator++()
		{
			for (size_t bitset_index{ _index / _BITSET_SIZE }; bitset_index < bitsets_ptr->size(); ++bitset_index)
			{
				size_t _bitset{ bitsets_ptr->at(bitset_index).to_ullong() };
				size_t bit_count{ _BITSET_SIZE - 1ULL - (_index % _BITSET_SIZE) };
				size_t mask{ ((1ULL << bit_count) - 1ULL) };

				_bitset &= mask;

				size_t count{ static_cast<size_t>(std::countl_zero(_bitset)) };

				if (count != _BITSET_SIZE)
				{
					_index = bitset_index * _BITSET_SIZE + count;
					break;
				}
				_index += _BITSET_SIZE - _index % _BITSET_SIZE;
			}

			return *this;
		}

		sparse_vector_iterator operator++(int)
		{
			++(*this);
			return sparse_vector_iterator{ *this };
		}

		bool operator==(const sparse_vector_iterator& left) const
		{
			return _index == left._index;
		}

		bool operator!=(const sparse_vector_iterator& left) const
		{
			return _index != left._index;
		}

		size_t index() const
		{
			return _index;
		}
	};

	template<typename T, typename Allocator = std::allocator<T>>
	class sparse_vector
	{
	private:
		using bitset64 = std::bitset<_BITSET_SIZE>;
		using bitset_vector = std::vector<bitset64>;
		using index_set = std::set<size_t>;
		using allocator_traits = std::allocator_traits<Allocator>;

	public:
		using value_type = T;
		using allocator_type = Allocator;
		using pointer = typename allocator_traits::pointer;
		using const_pointer = typename allocator_traits::const_pointer;
		using reference = T&;
		using const_reference = const T&;
		using size_type = typename allocator_traits::size_type;
		using difference_type = typename allocator_traits::difference_type;
		using iterator = sparse_vector_iterator<T>;
		using const_iterator = sparse_vector_iterator<const T>;

	private:
		pointer _data{ nullptr };
		bitset_vector bitsets;
		index_set indices;
		size_t _size{ 0 };
		size_t _capacity{ 0 };
		allocator_type allocator;

	public:
		sparse_vector(size_t initial_capacity = _BITSET_SIZE)
		{
			if (initial_capacity % _BITSET_SIZE != 0)
			{
				initial_capacity += _BITSET_SIZE - (initial_capacity % _BITSET_SIZE);
			}
			expand(initial_capacity);
		}

		sparse_vector(const sparse_vector& left)
			:sparse_vector{ left.copy() }
		{
		}

		sparse_vector(sparse_vector&& right) noexcept
			:_data{ right._data },
			bitsets{ std::move(right.bitsets) },
			indices{ std::move(right.indices) },
			_size{ right._size },
			_capacity{ right._capacity },
			allocator{ std::move(right.allocator) }
		{
			right._data = nullptr;
			right._size = 0;
			right._capacity = 0;
		}

		sparse_vector& operator=(const sparse_vector left)
		{
			(*this) = left.copy();
		}

		sparse_vector& operator=(sparse_vector&& right) noexcept
		{
			clear();

			_data = right._data;
			bitsets = std::move(right.bitsets);
			indices = std::move(right.indices);
			_size = right._size;
			_capacity = right._capacity;
			allocator = std::move(right.allocator);

			right._data = nullptr;
			right._size = 0;
			right._capacity = 0;
		}

		[[maybe_unused]] size_t push(const T& value)
		{
			return push(T{ value });
		}

		[[maybe_unused]] size_t push(T&& value)
		{
			if (indices.empty())
			{
				expand(2 * _capacity);
			}

			size_t bitset_index{ *indices.begin() };
			size_t index{ static_cast<size_t>(std::countl_zero(~bitsets[bitset_index].to_ullong())) };

			index += bitset_index * _BITSET_SIZE;

			_emplace(index, std::move(value));

			return index;
		}

		void insert(size_t index, const T& value)
		{
			insert(index, T{ value });
		}

		void insert(size_t index, T&& value)
		{
			_emplace(index, std::move(value));
		}

		template<class... Args>
		[[maybe_unused]] size_t emplace(Args&&... args)
		{
			size_t index{ free_index() };
			_emplace(index, std::move(args)...);

			return index;
		}

		void erase(size_t index)
		{
			size_t bitset_index{ index / _BITSET_SIZE };
			size_t bit_index{ index % _BITSET_SIZE };

			if (bitsets[bitset_index].all())
			{
				indices.insert(bitset_index);
			}

			bitsets[bitset_index].set(_BITSET_SIZE - 1ULL - bit_index, false);

			if (!std::is_trivially_destructible<T>::value)
			{
				destroy(&_data[index]);
			}

			--_size;
		}

		reference at(size_t index)
		{
			return _data[index];
		}

		const_reference at(size_t index) const
		{
			return _data[index];
		}

		reference operator[](size_t index)
		{
			return at(index);
		}

		const_reference operator[](size_t index) const
		{
			return at(index);
		}

		size_t size() const
		{
			return _size;
		}

		bool empty() const
		{
			return _size == 0;
		}

		size_t capacity() const
		{
			return _capacity;
		}

		void clear()
		{
			indices.clear();
			bitsets.clear();

			if (!std::is_trivially_destructible<T>::value)
			{
				for (auto& item : *this)
				{
					destroy(&item);
				}
			}

			allocator_traits::deallocate(allocator, _data, _capacity);

			_data = nullptr;
			_size = 0;
			_capacity = 0;
		}

		iterator begin()
		{
			return iterator{ _data, 0 , &bitsets };
		}

		iterator end()
		{
			return iterator{ _data, bitsets.size() * _BITSET_SIZE, nullptr };
		}

		const_iterator begin() const
		{
			return const_iterator{ _data, 0 , &bitsets };
		}

		const_iterator end() const
		{
			return const_iterator{ _data, bitsets.size() * _BITSET_SIZE, nullptr };
		}

		sparse_vector copy() const
		{
			sparse_vector out{ 0 };

			out.bitsets = bitsets;
			out.indices = indices;
			out.allocator = allocator;

			pointer out_data{ allocator_traits::allocate(out.allocator, _capacity) };

			const_iterator _begin{ begin() };
			const_iterator _end{ end() };

			for (; _begin != _end; ++_begin)
			{
				out_data[_begin.index()] = *_begin;
			}

			out._data = out_data;
			out._size = _size;
			out._capacity = _capacity;

			return out;
		}

		void shrink_to_fit()
		{
			if (empty())
			{
				clear();
				return;
			}

			size_t new_capacity{ _capacity };
			for (size_t bitset_index{ bitsets.size() - 1 }; bitset_index > 0; --bitset_index)
			{
				if (bitsets[bitset_index].any())
				{
					break;
				}
				new_capacity -= _BITSET_SIZE;
			}

			if (new_capacity != _capacity)
			{
				shrink(new_capacity);
			}
		}

		pointer data()
		{
			return _data;
		}

		const pointer data() const
		{
			return _data;
		}

	private:
		void expand(size_t new_capacity)
		{
			pointer temp{ _data };

			_data = allocator_traits::allocate(allocator, new_capacity);

			for (size_t index{ 0 }; index < _size; ++index)
			{
				T& item{ temp[index] };
				construct(_data + index, std::move(item));
				destroy(temp + index);
			}

			allocator_traits::deallocate(allocator, temp, _capacity);

			for (size_t bitset_index{ _capacity / _BITSET_SIZE }; bitset_index < new_capacity / _BITSET_SIZE; ++bitset_index)
			{
				indices.insert(bitset_index);
			}

			bitsets.resize(new_capacity / _BITSET_SIZE);

			_capacity = new_capacity;
		}

		void shrink(size_t new_capacity)
		{
			pointer temp{ _data };

			iterator it{ begin() };
			iterator _end{ end() };

			_data = allocator_traits::allocate(allocator, new_capacity);

			for (; it != _end; ++it)
			{
				construct(_data + it.index(), std::move(*it));
			}

			allocator_traits::deallocate(allocator, temp, _capacity);

			indices.clear();
			bitset_vector new_bitsets;

			for (size_t bitset_index{ 0 }; bitset_index < new_capacity / _BITSET_SIZE; ++bitset_index)
			{
				indices.insert(bitset_index);
				new_bitsets.push_back(bitsets[bitset_index]);
			}

			bitsets = new_bitsets;

			_capacity = new_capacity;
		}

		template<class... Args>
		void _emplace(size_t index, Args&&... args)
		{
			size_t bitset_index{ index / _BITSET_SIZE };
			size_t bit_index{ index % _BITSET_SIZE };

			bitsets[bitset_index].set(_BITSET_SIZE - 1ULL - bit_index);

			if (bitsets[bitset_index].all())
			{
				indices.erase(bitset_index);
			}

			construct(&_data[index], std::move(args)...);

			++_size;
		}

		size_t free_index()
		{
			if (indices.empty())
			{
				expand(2 * _capacity);
			}

			size_t bitset_index{ *indices.begin() };
			size_t index{ static_cast<size_t>(std::countl_zero(~bitsets[bitset_index].to_ullong())) };

			index += bitset_index * _BITSET_SIZE;

			return index;
		}

		template<class... Args>
		void construct(T* address, Args&&... args)
		{
			allocator_traits::construct(allocator, address, std::move(args)...);
		}

		void destroy(T* address)
		{
			allocator_traits::destroy(allocator, address);
		}
	};

}

#endif
