; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private constant [12 x i8] c"string: %s\0A\00"
@.str.1 = private constant [15 x i8] c"size of a: %d\0A\00"
@.str.2 = private constant [21 x i8] c"size of a.c_str: %d\0A\00"
@.str.3 = private constant [14 x i8] c"ptr of a: %p\0A\00"

define i32 @main() {
decl:
  br label %entry

entry:                                            ; preds = %decl
  call void @"_Ym.hello()"()
  ret i32 0
}

define internal void @"_Ym.hello()"() {
decl:
  %a = alloca { ptr, i64 }, align 8
  br label %entry

entry:                                            ; preds = %decl
  %0 = alloca [5 x i8], align 1
  store [5 x i8] c"hello", ptr %0, align 1
  %1 = insertvalue { ptr, i64 } undef, ptr %0, 0
  %2 = insertvalue { ptr, i64 } %1, i64 5, 1
  store { ptr, i64 } %2, ptr %a, align 8
  %ic.deref = load { ptr, i64 }, ptr %a, align 8
  %3 = call { ptr, i64 } @"_Ym.c_str(U8[)U8["({ ptr, i64 } %ic.deref)
  %4 = call i32 (ptr, ...) @printf(ptr @.str, { ptr, i64 } %3)
  %ic.deref1 = load { ptr, i64 }, ptr %a, align 8
  %5 = extractvalue { ptr, i64 } %ic.deref1, 1
  %6 = call i32 (ptr, ...) @printf(ptr @.str.1, i64 %5)
  %ic.deref2 = load { ptr, i64 }, ptr %a, align 8
  %7 = call { ptr, i64 } @"_Ym.c_str(U8[)U8["({ ptr, i64 } %ic.deref2)
  %8 = extractvalue { ptr, i64 } %7, 1
  %9 = call i32 (ptr, ...) @printf(ptr @.str.2, i64 %8)
  %ic.deref3 = load { ptr, i64 }, ptr %a, align 8
  %sl.ptr.x = extractvalue { ptr, i64 } %ic.deref3, 0
  %10 = call i32 (ptr, ...) @printf(ptr @.str.3, ptr %sl.ptr.x)
  ret void
}

define internal { ptr, i64 } @"_Ym.c_str(U8[)U8["({ ptr, i64 } %arg.str) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %str = alloca { ptr, i64 }, align 8
  store { ptr, i64 } %arg.str, ptr %str, align 8
  %0 = load { ptr, i64 }, ptr %str, align 8
  %1 = call { ptr, i64 } @"_Ym.dup_append(U8[,U8)U8["({ ptr, i64 } %0, i8 0)
  ret { ptr, i64 } %1
}

define internal { ptr, i64 } @"_Ym.dup_append(U8[,U8)U8["({ ptr, i64 } %arg.arr, i8 %arg.last) {
decl:
  %dup_t = alloca { ptr, i64 }, align 8
  br label %entry

entry:                                            ; preds = %decl
  %arr = alloca { ptr, i64 }, align 8
  store { ptr, i64 } %arg.arr, ptr %arr, align 8
  %last = alloca i8, align 1
  store i8 %arg.last, ptr %last, align 1
  %0 = load { ptr, i64 }, ptr %arr, align 8
  %1 = extractvalue { ptr, i64 } %0, 1
  %2 = add i64 %1, 1
  %3 = insertvalue { ptr, i64 } %0, i64 %2, 1
  store { ptr, i64 } %3, ptr %dup_t, align 8
  %4 = load { ptr, i64 }, ptr %arr, align 8
  %sl.ptr.x = extractvalue { ptr, i64 } %4, 0
  %ic.deref = load { ptr, i64 }, ptr %dup_t, align 8
  %sl.ptr.x1 = extractvalue { ptr, i64 } %ic.deref, 0
  %5 = load { ptr, i64 }, ptr %arr, align 8
  %6 = extractvalue { ptr, i64 } %5, 1
  call void @"_Ym.copy(U8*,U8*,I64)"(ptr %sl.ptr.x, ptr %sl.ptr.x1, i64 %6)
  %7 = load { ptr, i64 }, ptr %arr, align 8
  %8 = extractvalue { ptr, i64 } %7, 1
  %9 = load i8, ptr %last, align 1
  %10 = call i8 @"_Ym.[]=(U8[&,I64,U8)U8"(ptr %dup_t, i64 %8, i8 %9)
  %11 = load { ptr, i64 }, ptr %dup_t, align 8
  ret { ptr, i64 } %11
}

define internal void @"_Ym.copy(U8*,U8*,I64)"(ptr %arg.source, ptr %arg.dest, i64 %arg.amount) {
decl:
  %count = alloca i64, align 8
  br label %entry

entry:                                            ; preds = %decl
  %source = alloca ptr, align 8
  store ptr %arg.source, ptr %source, align 8
  %dest = alloca ptr, align 8
  store ptr %arg.dest, ptr %dest, align 8
  %amount = alloca i64, align 8
  store i64 %arg.amount, ptr %amount, align 8
  %0 = load i64, ptr %amount, align 8
  store i64 %0, ptr %count, align 8
  br label %while.test

while.test:                                       ; preds = %while.head, %entry
  %ic.deref = load i64, ptr %count, align 8
  %1 = icmp sgt i64 %ic.deref, 0
  br i1 %1, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %ic.deref1 = load i64, ptr %count, align 8
  %2 = sub i64 %ic.deref1, 1
  store i64 %2, ptr %count, align 8
  %3 = load ptr, ptr %dest, align 8
  %ic.deref2 = load i64, ptr %count, align 8
  %4 = load ptr, ptr %source, align 8
  %ic.deref3 = load i64, ptr %count, align 8
  %p.get_at.gep = getelementptr i8, ptr %4, i64 %ic.deref3
  %ic.deref4 = load i8, ptr %p.get_at.gep, align 1
  %p.set_at.gep = getelementptr i8, ptr %3, i64 %ic.deref2
  store i8 %ic.deref4, ptr %p.set_at.gep, align 1
  br label %while.test

while.merge:                                      ; preds = %while.test
  ret void
}

define internal i8 @"_Ym.[]=(U8[&,I64,U8)U8"(ptr %arg.slice, i64 %arg.offset, i8 %arg.val) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, ptr %offset, align 8
  %val = alloca i8, align 1
  store i8 %arg.val, ptr %val, align 1
  %ic.deref = load { ptr, i64 }, ptr %arg.slice, align 8
  %sl.ptr.x = extractvalue { ptr, i64 } %ic.deref, 0
  %0 = load i64, ptr %offset, align 8
  %1 = load i8, ptr %val, align 1
  %p.set_at.gep = getelementptr i8, ptr %sl.ptr.x, i64 %0
  store i8 %1, ptr %p.set_at.gep, align 1
  %2 = load i8, ptr %val, align 1
  ret i8 %2
}

declare i32 @printf(ptr, ...)
