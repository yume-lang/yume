; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define i32 @main() {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %0 = call i32 @"_Ym.foo(I32,I32,I32)I32"(i32 1, i32 2, i32 3)
  %1 = call i32 @"_Ym.foo(I32,I32)I32"(i32 4, i32 5)
  %2 = call i8 @"_Ym.foo(U8,I32)U8"(i8 97, i32 4)
  %3 = call i8 @"_Ym.foo(I32,U8)U8"(i32 5, i8 98)
  ret i32 0
}

define internal i32 @"_Ym.foo(I32,I32,I32)I32"(i32 %arg.a, i32 %arg.b, i32 %arg.c) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i32, align 4
  store i32 %arg.a, i32* %a, align 4
  %b = alloca i32, align 4
  store i32 %arg.b, i32* %b, align 4
  %c = alloca i32, align 4
  store i32 %arg.c, i32* %c, align 4
  %0 = load i32, i32* %a, align 4
  ret i32 %0
}

define internal i32 @"_Ym.foo(I32,I32)I32"(i32 %arg.a, i32 %arg.b) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i32, align 4
  store i32 %arg.a, i32* %a, align 4
  %b = alloca i32, align 4
  store i32 %arg.b, i32* %b, align 4
  %0 = load i32, i32* %b, align 4
  ret i32 %0
}

define internal i8 @"_Ym.foo(U8,I32)U8"(i8 %arg.a, i32 %arg.b) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i8, align 1
  store i8 %arg.a, i8* %a, align 1
  %b = alloca i32, align 4
  store i32 %arg.b, i32* %b, align 4
  %0 = load i8, i8* %a, align 1
  ret i8 %0
}

define internal i8 @"_Ym.foo(I32,U8)U8"(i32 %arg.a, i8 %arg.b) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i32, align 4
  store i32 %arg.a, i32* %a, align 4
  %b = alloca i8, align 1
  store i8 %arg.b, i8* %b, align 1
  %0 = load i8, i8* %b, align 1
  ret i8 %0
}
