# TODO

- Implement Frame Encoder
  - Need to add unittests for Frame Encoder
- Implement Frame Decoder
  - Frame decoder is very very simple
  - Need to add unittests for Frame Decoder

# Code Generation Requirements

✅ Namespace form:
namespace ncore 
{
    namespace nudt
    {   
    }
}

✅ No nudt_ prefixes anywhere
✅ No std usage:
  - for system types use; u8, u16, u32, u64, i8, i16, i32, i64, f32, f64
  - for memset use g_memset
  - for memcpy use g_memcpy
  - for size_t use uint_t
✅ C++‑style struct definitions
✅ typedef style function pointer declarations, e.g. typedef void (*my_fn)(int arg);
✅ Only // comments
✅ All static (inline) functions prefixed with 's_'
✅ All function pointer typedefs postfixed with _fn
✅ All struct names postfixed with _t, e.g. rx_ops_t, rx_t, tx_t, etc.
✅ All struct members postfixed with m_, e.g. m_alloc_object_data, m_in_flight, m_hash32
