; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define i32 @main() {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %0 = call i32 @"_Ym.larger(I32,I32)I32"(i32 3, i32 7)
  %1 = icmp eq i32 %0, 7
  %2 = call i1 @"_Ym.!(Bool)Bool"(i1 %1)
  %3 = zext i1 %2 to i32
  ret i32 %3
}

define internal i32 @"_Ym.larger(I32,I32)I32"(i32 %arg.a, i32 %arg.b) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i32, align 4
  store i32 %arg.a, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 %arg.b, ptr %b, align 4
  br label %if.test

if.test:                                          ; preds = %entry
  %0 = load i32, ptr %a, align 4
  %1 = load i32, ptr %b, align 4
  %2 = icmp sgt i32 %0, %1
  br i1 %2, label %if.then, label %if.else

if.then:                                          ; preds = %if.test
  %3 = load i32, ptr %a, align 4
  ret i32 %3

if.else:                                          ; preds = %if.test
  %4 = load i32, ptr %b, align 4
  ret i32 %4
}

define internal i1 @"_Ym.!(Bool)Bool"(i1 %arg.a) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i1, align 1
  store i1 %arg.a, ptr %a, align 1
  br label %if.test

if.test:                                          ; preds = %entry
  %0 = load i1, ptr %a, align 1
  br i1 %0, label %if.then, label %if.else

if.then:                                          ; preds = %if.test
  ret i1 false

if.else:                                          ; preds = %if.test
  ret i1 true
}
