#ifndef _ONNX_SPECSTRINGS_H
#define _ONNX_SPECSTRINGS_H

// MinGW/GCC compatibility for Microsoft SAL annotations
// These are typically empty macros for GCC

#define _In_
#define _In_z_
#define _In_opt_
#define _In_opt_z_
#define _Out_
#define _Outptr_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _Frees_ptr_opt_
#define _Ret_maybenull_
#define _Ret_notnull_
#define _Check_return_
#define _Outptr_result_maybenull_        // Used without parameter
#define _Outptr_result_maybenull_(x)     // Used with parameter
#define _In_reads_(x)
#define _Inout_updates_(x)
#define _Out_writes_(x)
#define _Inout_updates_all_(x)
#define _Out_writes_bytes_all_(x)
#define _Out_writes_all_(x)
#define _Success_(x)
#define _Outptr_result_buffer_maybenull_(x)
#define _Return_type_success_(x)

#endif // _ONNX_SPECSTRINGS_H
