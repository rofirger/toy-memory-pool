#ifndef _MEMORY_POOL_H_
#define _MEMORY_POOL_H_
#include <cstddef>
#include <stdio.h>
#include <malloc.h>
#include <new>
#include <vector>
#include <mutex>
#pragma pack(push)
#ifdef _WIN64
#pragma pack(8)
#else
#pragma pack(4)
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:26451)
#pragma warning(disable:6297)
#pragma warning(disable:6386)
#endif
namespace rofirger
{
	class MemoryPool
	{
	public:
		typedef char		   blksts_t;
		typedef unsigned char  blkpow_t;

		static const blkpow_t _arrsize = 32;

		typedef struct _memory_header
		{
			struct _memory_header* _llink;					 // lead node pointer
			struct _memory_header* _rlink;					 // rear node pointer
			struct _memory_header* _offset_base_addr;		 // this block is divided from chunk. Serve only as a base address!
			blksts_t _tag;									 // block status. 0:free block, 1:occupied block
			blkpow_t _pval;									 // block size = pow(2, pval)
			blkpow_t _dc_diff;								 // Add 1 when block is divided once, subtract 1 when combined.
		}_memory_header, * _ptrmemory_header;

	private:
		typedef struct _pool_base_info
		{
			MemoryPool* _ptr_pool;
			_ptrmemory_header* _table;
		}_pool_base_info;
		static size_t _pools;
		static std::vector<_pool_base_info*> _stack_pool_vec;
		static std::vector<_pool_base_info*> _heap_pool_vec;

		typedef struct ValidIndexRet
		{
			blkpow_t _real_table_index;
			blkpow_t _idea_table_index;
		}ValidIndexRet;

	private:
		std::recursive_mutex _malloc_recursive_mutex;
		_ptrmemory_header _table[_arrsize] = { nullptr };
		// Indicates whether the list of the subtable contains unallocated nodes
		size_t _subtable_blocks_node_unallocated_size[_arrsize] = { 0 };
		blkpow_t _init_pow = 0;
		int _init_pow_block_num = 0;
	private:
		inline blkpow_t FindPow(const size_t _target_)
		{
			size_t num_ = _target_ - 1;
			blkpow_t ret_ = 1;
			while (num_ >>= 1)
			{
				ret_++;
			}
			return ret_;
		}

		inline bool IsTableNull()
		{
			for (blkpow_t i_ = 0; i_ < _arrsize; ++i_)
			{
				if (_table[i_] != nullptr) return false;
			}
			return true;
		}

		void InserBlock(void* _block_, int _pow_)
		{
			_ptrmemory_header block_ = reinterpret_cast<_ptrmemory_header>(_block_);
			if (_table[_pow_] != nullptr)
			{
				block_->_llink = _table[_pow_]->_llink;
				_table[_pow_]->_llink->_rlink = block_;
				_table[_pow_]->_llink = block_;
				block_->_rlink = _table[_pow_];
				return;
			}
			block_->_rlink = block_;
			block_->_llink = block_;
			_table[_pow_] = block_;
			_subtable_blocks_node_unallocated_size[_pow_] += ((1 << _pow_) & (~block_->_tag));
		}

		void InsertBlockIntoSubTable(const blkpow_t& _target_pow_, const int _insert_num_) throw(std::bad_alloc)
		{
			for (blkpow_t i_ = 0; i_ < _insert_num_; ++i_)
			{
				void* block_ = malloc((1 << _target_pow_) + sizeof(_memory_header));
				if (block_ == NULL)
					throw std::bad_alloc();
				new(block_) _memory_header{
					reinterpret_cast<_ptrmemory_header>(block_),
					reinterpret_cast<_ptrmemory_header>(block_),
					reinterpret_cast<_ptrmemory_header>(block_),
					0, _target_pow_, 0
				};
				InserBlock(block_, _target_pow_);
			}
			_subtable_blocks_node_unallocated_size[_target_pow_] = ((1 << _target_pow_) * _insert_num_);
		}

		void ExpandPool(const blkpow_t& _lack_pow_, const int _insert_num_ = 2) throw(std::bad_alloc)
		{
			InsertBlockIntoSubTable(_lack_pow_, _insert_num_);
		}



		ValidIndexRet FindValidTableIndex(const size_t& _n_)
		{
			blkpow_t first_ = FindPow(_n_);
			for (blkpow_t i_ = first_; i_ < _arrsize; ++i_)
			{
				if (_table[i_] != nullptr && _subtable_blocks_node_unallocated_size[i_] != 0)
				{
					return ValidIndexRet{ i_,first_ };
				}
			}
			ExpandPool(first_);
			return ValidIndexRet{ first_,first_ };
		}

		/*
		* warning: be sure there are free block in _table[_pow_] before calling void* FindFreeBlock(const blkpow_t _pow_)
		*/
		void* FindFreeBlock(const blkpow_t _pow_)
		{
			_ptrmemory_header p_ = _table[_pow_];
			if (p_->_tag == 0)
			{
				return p_;
			}
			while (p_->_rlink != _table[_pow_])
			{
				if (p_->_rlink->_tag == 0)
				{
					return p_->_rlink;
				}
				p_ = p_->_rlink;
			}
			// avoid this return value
			return nullptr;
		}

		void* BlockDivision(const ValidIndexRet& _valid_index_, void* _block_addr_)
		{
			_ptrmemory_header p_ = reinterpret_cast<_ptrmemory_header>(_block_addr_);
			for (blkpow_t i_ = _valid_index_._idea_table_index; i_ < _valid_index_._real_table_index; ++i_)
			{
				void* p_division_block_ = reinterpret_cast<void*>(reinterpret_cast<char*>(_block_addr_) + (1 << i_));
				new(p_division_block_) _memory_header{ nullptr,nullptr,p_->_offset_base_addr,0,i_,0 };
				InserBlock(p_division_block_, i_);
			}
			PopBlockOut(_block_addr_);
			p_->_pval = _valid_index_._idea_table_index;
			p_->_tag = 1;
			reinterpret_cast<_ptrmemory_header>(p_->_offset_base_addr)->_dc_diff += (_valid_index_._real_table_index - _valid_index_._idea_table_index);
			InserBlock(_block_addr_, p_->_pval);
			return _block_addr_;
		}

		void* FindBuddy(void* _block_)
		{
			_ptrmemory_header p_ = reinterpret_cast<_ptrmemory_header>(_block_);
			if (reinterpret_cast<_ptrmemory_header>(p_->_offset_base_addr)->_dc_diff == 0)
			{
				return nullptr;
			}
			std::ptrdiff_t diff_ = reinterpret_cast<char*>(_block_) - reinterpret_cast<char*>(p_->_offset_base_addr);
			if (diff_ % (1 << (p_->_pval + 1)) == 0)
			{
				return reinterpret_cast<void*>(reinterpret_cast<char*>(_block_) + (1 << p_->_pval));
			}
			return reinterpret_cast<void*>(reinterpret_cast<char*>(_block_) - (1 << p_->_pval));
		}

		void PopBlockOut(void* _block_)
		{
			_ptrmemory_header p_ = reinterpret_cast<_ptrmemory_header>(_block_);
			if (p_ == _table[p_->_pval])
			{
				if (p_->_llink != p_)
					_table[p_->_pval] = p_->_rlink;
				else
					_table[p_->_pval] = nullptr;
			}
			p_->_llink->_rlink = p_->_rlink;
			p_->_rlink->_llink = p_->_llink;
			_subtable_blocks_node_unallocated_size[p_->_pval] -= ((1 << p_->_pval) & (~p_->_tag));
		}

		void ZeroAllBlockTag()
		{
			for (blkpow_t i_ = 0; i_ < _arrsize; ++i_)
			{
				if (_table[i_] != nullptr)
				{
					_ptrmemory_header p_ = reinterpret_cast<_ptrmemory_header>(_table[i_]);
					p_->_tag = 0;
					while (p_->_rlink != _table[i_])
					{
						p_->_rlink->_tag = 0;
						p_ = p_->_rlink;
					}
				}
			}
		}

	public:
		MemoryPool()
		{
			++_pools;
		}
		MemoryPool(const int _init_pow_, const int _init_pow_block_num_)
		{
			++_pools;
			_init_pow = _init_pow_, _init_pow_block_num = _init_pow_block_num_;
			InsertBlockIntoSubTable(_init_pow, _init_pow_block_num);
		}
		bool SetPool(const int _init_pow_, const int _init_pow_block_num_)
		{
			if (IsTableNull())
			{
				_init_pow = _init_pow_, _init_pow_block_num = _init_pow_block_num_;
				InsertBlockIntoSubTable(_init_pow, _init_pow_block_num);
				return true;
			}
			return false;
		}

		/*
		* ordered_malloc function must have "mutex"
		*/
		void* ordered_malloc(size_t __n)noexcept
		{
			size_t real_alloc_ = __n + sizeof(_memory_header);
			std::lock_guard<std::recursive_mutex> lock_(_malloc_recursive_mutex);
			ValidIndexRet valid_index_ret_ = FindValidTableIndex(real_alloc_);
			void* free_block_ = FindFreeBlock(valid_index_ret_._real_table_index);
			void* malloc_block_ = free_block_;
			if (valid_index_ret_._idea_table_index != valid_index_ret_._real_table_index)
			{
				malloc_block_ = BlockDivision(valid_index_ret_, free_block_);
			}
			else
			{
				_ptrmemory_header p_temp_ = reinterpret_cast<_ptrmemory_header>(malloc_block_);
				p_temp_->_tag = 1;
				_subtable_blocks_node_unallocated_size[p_temp_->_pval] -= (1 << p_temp_->_pval);
			}
			return reinterpret_cast<void*>(reinterpret_cast<char*>(malloc_block_) + sizeof(_memory_header));
		}

		/*
		* not include header. The _block_ just is user's memory.
		*/
		void ordered_free(void* _block_)
		{
			_ptrmemory_header p_header_ = reinterpret_cast<_ptrmemory_header>(reinterpret_cast<char*>(_block_) - sizeof(_memory_header));
			void* p_buddy_void_ = FindBuddy(reinterpret_cast<void*>(p_header_));
			if (p_buddy_void_ == nullptr)
			{
				p_header_->_tag = 0;
				return;
			}
			_ptrmemory_header p_buddy_header_ = reinterpret_cast<_ptrmemory_header>(p_buddy_void_);
			if (p_buddy_header_->_tag == 0)
			{
				std::ptrdiff_t diff_ = reinterpret_cast<char*>(_block_) - reinterpret_cast<char*>(p_buddy_void_);
				PopBlockOut(reinterpret_cast<void*>(p_header_));
				PopBlockOut(p_buddy_void_);
				if (diff_ < 0)
				{
					p_header_->_pval++;
					InserBlock(reinterpret_cast<void*>(p_header_), p_header_->_pval);
				}
				else
				{
					p_buddy_header_->_pval++;
					InserBlock(p_buddy_void_, p_buddy_header_->_pval);
				}
				reinterpret_cast<_ptrmemory_header>(p_header_->_offset_base_addr)->_dc_diff--;
			}

			p_header_->_tag = 0;
		}

		void OrderedFreeAll()
		{
			void* p_user_memory_area_ = nullptr;
			for (blkpow_t i_ = 0; i_ < _arrsize; ++i_)
			{
				if (_table[i_] != nullptr)
				{
					p_user_memory_area_ = reinterpret_cast<void*>(reinterpret_cast<char*>(_table[i_]) + sizeof(_memory_header));
					ordered_free(p_user_memory_area_);
					_ptrmemory_header p_ = _table[i_];
					if (nullptr == p_)
						continue;
					while (p_->_rlink != _table[i_])
					{
						p_user_memory_area_ = reinterpret_cast<void*>(reinterpret_cast<char*>(p_->_rlink) + sizeof(_memory_header));
						ordered_free(p_user_memory_area_);
						if (nullptr == _table[i_])
							break;;
						p_ = p_->_rlink;
					}
				}
			}
		}

		void operator delete(void* _ptr_)
		{
			if (_ptr_)
				::free(_ptr_);
		}

		void operator delete[](void* _ptr_)
		{
			if (_ptr_)
				::free(_ptr_);
		}

			void* operator new(std::size_t __size, void* __p)
		{
			(void)__size;
			return __p;
		}

		/*
		* note: Any allocation function for a class is a static member (even if not explicitly declared static).
		*/
		void* operator new(std::size_t __size) throw(std::bad_alloc)
		{
			if (__size == 0) { __size = 1; }
			void* malloc_memory_ = ::malloc(__size);
			while (malloc_memory_ == nullptr)
			{
				std::new_handler malloc_new_handler_ = std::get_new_handler();
				if (malloc_new_handler_)
					malloc_new_handler_();
				else
					throw std::bad_alloc();
			}
			return malloc_memory_;
		}

		/*
		* note: Any allocation function for a class is a static member (even if not explicitly declared static).
		*/
		void* operator new[](std::size_t __size) throw(std::bad_alloc)
		{
			if (__size == 0) { __size = 1; }
			void* malloc_memory_ = ::malloc(__size);
			while (malloc_memory_ == nullptr)
			{
				std::new_handler malloc_new_handler_ = std::get_new_handler();
				if (malloc_new_handler_)
					malloc_new_handler_();
				else
					throw std::bad_alloc();
			}
			return malloc_memory_;
		}

	public:
		static size_t GetPools() { return _pools; }

		void PrintPoolTableBasicInfo2Console()
		{
			printf("\r\n");
			printf("+----------------------------------------+");
			printf("\r\n");
			printf("|*-*-* MEMORY POOL TABLE BASIC INFO *-*-*|");
			printf("\r\n");
			printf("+----------------------------------------+");
			printf("\r\n");
			for (blkpow_t i_ = 0; i_ < _arrsize; ++i_)
			{
				if (_table[i_] != nullptr)
				{
					_ptrmemory_header p_ = _table[i_];
					printf("-- INDEX:	%d", i_);
					printf("\r\n");
					do
					{
						printf("llink: %p\trlink: %p\toffset_base_addr: %p\ttag: %d\tpval: %d\tdc_diff: %d\r\n",
							p_->_llink, p_->_rlink, p_->_offset_base_addr, p_->_tag, p_->_pval, p_->_dc_diff);
					} while ((p_ = p_->_rlink) != _table[i_]);
					printf("\r\n\r\n");
				}
			}
		}

		~MemoryPool()
		{
			// "free" all memory blocks and then uniformly release.
			ZeroAllBlockTag();
			OrderedFreeAll();
			for (blkpow_t i_ = 0; i_ < _arrsize; ++i_)
			{
				_ptrmemory_header p_ = _table[i_];
				if (p_ != nullptr)
				{
					p_->_llink->_rlink = nullptr;
					while (p_->_rlink != nullptr)
					{
						p_ = p_->_rlink;
						this->operator  delete (reinterpret_cast<void*>(p_->_llink));
					}
					this->operator delete (reinterpret_cast<void*>(p_));
				}
			}
			--_pools;
		}

	};


	template<typename T>
	class allocator
	{
	public:
		typedef T			 value_type;
		typedef T* pointer;
		typedef const T* const_pointer;
		typedef T& reference;
		typedef const T& const_reference;
		typedef std::size_t		 size_type;
		typedef std::ptrdiff_t    difference_type;
		template<typename U>
		struct rebind
		{
			typedef allocator<U> other;
		};
	public:
		allocator();
		allocator(const allocator& _other_allocator);
		template<typename U>allocator(const allocator<U>& _g_allocator);
		~allocator();

		pointer allocate(size_type n, const void* hint = 0);
		void deallocate(pointer p, size_type n);
		void construct(pointer p, const_reference value);
		void destroy(pointer p);
		pointer address(reference r);
		const_pointer const_address(const_reference cr);
		size_type max_size()const;

	};
}
size_t rofirger::MemoryPool::_pools = 0;

#pragma pack(pop)

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // !_MEMORY_POOL_H_
