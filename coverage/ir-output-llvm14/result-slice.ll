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
  %a = alloca { i8*, i64 }, align 8
  br label %entry

entry:                                            ; preds = %decl
  %0 = alloca [5 x i8], align 1
  store [5 x i8] c"hello", [5 x i8]* %0, align 1
  %1 = bitcast [5 x i8]* %0 to i8*
  %2 = insertvalue { i8*, i64 } undef, i8* %1, 0
  %3 = insertvalue { i8*, i64 } %2, i64 5, 1
  store { i8*, i64 } %3, { i8*, i64 }* %a, align 8
  %ic.deref = load { i8*, i64 }, { i8*, i64 }* %a, align 8
  %4 = call { i8*, i64 } @"_Ym.c_str(U8[)U8["({ i8*, i64 } %ic.deref)
  %5 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str, i32 0, i32 0), { i8*, i64 } %4)
  %ic.deref1 = load { i8*, i64 }, { i8*, i64 }* %a, align 8
  %6 = extractvalue { i8*, i64 } %ic.deref1, 1
  %7 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @.str.1, i32 0, i32 0), i64 %6)
  %ic.deref2 = load { i8*, i64 }, { i8*, i64 }* %a, align 8
  %8 = call { i8*, i64 } @"_Ym.c_str(U8[)U8["({ i8*, i64 } %ic.deref2)
  %9 = extractvalue { i8*, i64 } %8, 1
  %10 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([21 x i8], [21 x i8]* @.str.2, i32 0, i32 0), i64 %9)
  %ic.deref3 = load { i8*, i64 }, { i8*, i64 }* %a, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %ic.deref3, 0
  %11 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([14 x i8], [14 x i8]* @.str.3, i32 0, i32 0), i8* %sl.ptr.x)
  ret void
}

define internal { i8*, i64 } @"_Ym.c_str(U8[)U8["({ i8*, i64 } %arg.str) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %str = alloca { i8*, i64 }, align 8
  store { i8*, i64 } %arg.str, { i8*, i64 }* %str, align 8
  %0 = load { i8*, i64 }, { i8*, i64 }* %str, align 8
  %1 = call { i8*, i64 } @"_Ym.dup_append(U8[,U8)U8["({ i8*, i64 } %0, i8 0)
  ret { i8*, i64 } %1
}

define internal { i8*, i64 } @"_Ym.dup_append(U8[,U8)U8["({ i8*, i64 } %arg.arr, i8 %arg.last) {
decl:
  %dup_t = alloca { i8*, i64 }, align 8
  br label %entry

entry:                                            ; preds = %decl
  %arr = alloca { i8*, i64 }, align 8
  store { i8*, i64 } %arg.arr, { i8*, i64 }* %arr, align 8
  %last = alloca i8, align 1
  store i8 %arg.last, i8* %last, align 1
  %0 = load { i8*, i64 }, { i8*, i64 }* %arr, align 8
  %1 = extractvalue { i8*, i64 } %0, 1
  %2 = add i64 %1, 1
  %3 = insertvalue { i8*, i64 } %0, i64 %2, 1
  store { i8*, i64 } %3, { i8*, i64 }* %dup_t, align 8
  %4 = load { i8*, i64 }, { i8*, i64 }* %arr, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %4, 0
  %ic.deref = load { i8*, i64 }, { i8*, i64 }* %dup_t, align 8
  %sl.ptr.x1 = extractvalue { i8*, i64 } %ic.deref, 0
  %5 = load { i8*, i64 }, { i8*, i64 }* %arr, align 8
  %6 = extractvalue { i8*, i64 } %5, 1
  call void @"_Ym.copy(U8*,U8*,I64)"(i8* %sl.ptr.x, i8* %sl.ptr.x1, i64 %6)
  %7 = load { i8*, i64 }, { i8*, i64 }* %arr, align 8
  %8 = extractvalue { i8*, i64 } %7, 1
  %9 = load i8, i8* %last, align 1
  %10 = call i8 @"_Ym.[]=(U8[&,I64,U8)U8"({ i8*, i64 }* %dup_t, i64 %8, i8 %9)
  %11 = load { i8*, i64 }, { i8*, i64 }* %dup_t, align 8
  ret { i8*, i64 } %11
}

define internal void @"_Ym.copy(U8*,U8*,I64)"(i8* %arg.source, i8* %arg.dest, i64 %arg.amount) {
decl:
  %count = alloca i64, align 8
  br label %entry

entry:                                            ; preds = %decl
  %source = alloca i8*, align 8
  store i8* %arg.source, i8** %source, align 8
  %dest = alloca i8*, align 8
  store i8* %arg.dest, i8** %dest, align 8
  %amount = alloca i64, align 8
  store i64 %arg.amount, i64* %amount, align 8
  %0 = load i64, i64* %amount, align 8
  store i64 %0, i64* %count, align 8
  br label %while.test

while.test:                                       ; preds = %while.head, %entry
  %ic.deref = load i64, i64* %count, align 8
  %1 = icmp sgt i64 %ic.deref, 0
  br i1 %1, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %ic.deref1 = load i64, i64* %count, align 8
  %2 = sub i64 %ic.deref1, 1
  store i64 %2, i64* %count, align 8
  %3 = load i8*, i8** %dest, align 8
  %ic.deref2 = load i64, i64* %count, align 8
  %4 = load i8*, i8** %source, align 8
  %ic.deref3 = load i64, i64* %count, align 8
  %p.get_at.gep = getelementptr i8, i8* %4, i64 %ic.deref3
  %ic.deref4 = load i8, i8* %p.get_at.gep, align 1
  %p.set_at.gep = getelementptr i8, i8* %3, i64 %ic.deref2
  store i8 %ic.deref4, i8* %p.set_at.gep, align 1
  br label %while.test

while.merge:                                      ; preds = %while.test
  ret void
}

define internal i8 @"_Ym.[]=(U8[&,I64,U8)U8"({ i8*, i64 }* %arg.slice, i64 %arg.offset, i8 %arg.val) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %val = alloca i8, align 1
  store i8 %arg.val, i8* %val, align 1
  %ic.deref = load { i8*, i64 }, { i8*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %1 = load i8, i8* %val, align 1
  %p.set_at.gep = getelementptr i8, i8* %sl.ptr.x, i64 %0
  store i8 %1, i8* %p.set_at.gep, align 1
  %2 = load i8, i8* %val, align 1
  ret i8 %2
}

declare i32 @printf(i8*, ...)
