; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%_Instruction = type { i32, i32 }

@.str = private constant [107 x i8] c"++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.\00"
@.str.2 = private constant [3 x i8] c"%c\00"

define i32 @main(i32 %arg.argc, i8** %arg.argv) {
decl:
  %word_ptr = alloca i8*, align 8
  %word = alloca { i8*, i64 }, align 8
  %sl.ctor.definit.iter = alloca i64, align 8
  %instructions = alloca { %_Instruction*, i64 }, align 8
  %pc = alloca i32, align 4
  %ptr = alloca i32, align 4
  %memory = alloca { i8*, i64 }, align 8
  %sl.ctor.definit.iter7 = alloca i64, align 8
  %instr = alloca %_Instruction, align 8
  br label %entry

entry:                                            ; preds = %decl
  %argc = alloca i32, align 4
  store i32 %arg.argc, i32* %argc, align 4
  %argv = alloca i8**, align 8
  store i8** %arg.argv, i8*** %argv, align 8
  store i8* getelementptr inbounds ([107 x i8], [107 x i8]* @.str, i32 0, i32 0), i8** %word_ptr, align 8
  br label %if.test

if.test:                                          ; preds = %entry
  %0 = load i32, i32* %argc, align 4
  %1 = icmp sgt i32 %0, 1
  br i1 %1, label %if.then, label %if.test1

if.then:                                          ; preds = %if.test
  %2 = load i8**, i8*** %argv, align 8
  %p.get_at.gep = getelementptr i8*, i8** %2, i64 1
  %c.nmut.deref = load i8*, i8** %p.get_at.gep, align 8
  store i8* %c.nmut.deref, i8** %word_ptr, align 8
  br label %if.cont

if.test1:                                         ; preds = %if.test
  br label %if.cont

if.cont:                                          ; preds = %if.test1, %if.then
  %ic.deref = load i8*, i8** %word_ptr, align 8
  %3 = call i64 @"_Ym.c_len(U8*)I64"(i8* %ic.deref)
  %sl.ctor.size = trunc i64 %3 to i32
  %mallocsize = mul i32 %sl.ctor.size, ptrtoint (i8* getelementptr (i8, i8* null, i32 1) to i32)
  %4 = tail call i8* @malloc(i32 %mallocsize)
  %5 = insertvalue { i8*, i64 } undef, i8* %4, 0
  %sl.ctor.inst = insertvalue { i8*, i64 } %5, i64 %3, 1
  store i64 0, i64* %sl.ctor.definit.iter, align 8
  br label %sl.ctor.definit.test

sl.ctor.definit.test:                             ; preds = %sl.ctor.definit.head, %if.cont
  %6 = load i64, i64* %sl.ctor.definit.iter, align 8
  %sl.ctor.definit.cmp = icmp slt i64 %6, %3
  br i1 %sl.ctor.definit.cmp, label %sl.ctor.definit.head, label %sl.ctor.definit.merge

sl.ctor.definit.head:                             ; preds = %sl.ctor.definit.test
  %7 = load i64, i64* %sl.ctor.definit.iter, align 8
  %sl.ctor.definit.gep = getelementptr inbounds i8, i8* %4, i64 %7
  store i8 0, i8* %sl.ctor.definit.gep, align 1
  %8 = load i64, i64* %sl.ctor.definit.iter, align 8
  %9 = add i64 %8, 1
  store i64 %9, i64* %sl.ctor.definit.iter, align 8
  br label %sl.ctor.definit.test

sl.ctor.definit.merge:                            ; preds = %sl.ctor.definit.test
  store { i8*, i64 } %sl.ctor.inst, { i8*, i64 }* %word, align 8
  %ic.deref2 = load i8*, i8** %word_ptr, align 8
  %ic.deref3 = load { i8*, i64 }, { i8*, i64 }* %word, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %ic.deref3, 0
  %ic.deref4 = load { i8*, i64 }, { i8*, i64 }* %word, align 8
  %10 = extractvalue { i8*, i64 } %ic.deref4, 1
  call void @"_Ym.copy(U8*,U8*,I64)"(i8* %ic.deref2, i8* %sl.ptr.x, i64 %10)
  %ic.deref5 = load { i8*, i64 }, { i8*, i64 }* %word, align 8
  %11 = call { %_Instruction*, i64 } @"_Ym.parse(U8[)Instruction["({ i8*, i64 } %ic.deref5)
  store { %_Instruction*, i64 } %11, { %_Instruction*, i64 }* %instructions, align 8
  store i32 0, i32* %pc, align 4
  store i32 0, i32* %ptr, align 4
  %12 = tail call i8* @malloc(i32 mul (i32 ptrtoint (i8* getelementptr (i8, i8* null, i32 1) to i32), i32 65535))
  %13 = insertvalue { i8*, i64 } undef, i8* %12, 0
  %sl.ctor.inst6 = insertvalue { i8*, i64 } %13, i64 65535, 1
  store i64 0, i64* %sl.ctor.definit.iter7, align 8
  br label %sl.ctor.definit.test8

sl.ctor.definit.test8:                            ; preds = %sl.ctor.definit.head9, %sl.ctor.definit.merge
  %14 = load i64, i64* %sl.ctor.definit.iter7, align 8
  %sl.ctor.definit.cmp11 = icmp slt i64 %14, 65535
  br i1 %sl.ctor.definit.cmp11, label %sl.ctor.definit.head9, label %sl.ctor.definit.merge10

sl.ctor.definit.head9:                            ; preds = %sl.ctor.definit.test8
  %15 = load i64, i64* %sl.ctor.definit.iter7, align 8
  %sl.ctor.definit.gep12 = getelementptr inbounds i8, i8* %12, i64 %15
  store i8 0, i8* %sl.ctor.definit.gep12, align 1
  %16 = load i64, i64* %sl.ctor.definit.iter7, align 8
  %17 = add i64 %16, 1
  store i64 %17, i64* %sl.ctor.definit.iter7, align 8
  br label %sl.ctor.definit.test8

sl.ctor.definit.merge10:                          ; preds = %sl.ctor.definit.test8
  store { i8*, i64 } %sl.ctor.inst6, { i8*, i64 }* %memory, align 8
  br label %while.test

while.test:                                       ; preds = %if.cont18, %sl.ctor.definit.merge10
  %ic.deref13 = load i32, i32* %pc, align 4
  %ic.int = sext i32 %ic.deref13 to i64
  %ic.deref14 = load { %_Instruction*, i64 }, { %_Instruction*, i64 }* %instructions, align 8
  %18 = extractvalue { %_Instruction*, i64 } %ic.deref14, 1
  %19 = icmp slt i64 %ic.int, %18
  br i1 %19, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %ic.deref15 = load i32, i32* %pc, align 4
  %ic.int16 = sext i32 %ic.deref15 to i64
  %20 = call %_Instruction* @"_Ym.[](Instruction[&,I64)Instruction&"({ %_Instruction*, i64 }* %instructions, i64 %ic.int16)
  %c.nmut.deref17 = load %_Instruction, %_Instruction* %20, align 4
  store %_Instruction %c.nmut.deref17, %_Instruction* %instr, align 4
  br label %if.test19

while.merge:                                      ; preds = %while.test
  ret i32 0

if.test19:                                        ; preds = %while.head
  %21 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type = extractvalue %_Instruction %21, 0
  %22 = icmp eq i32 %s.field.type, 1
  br i1 %22, label %if.then20, label %if.test21

if.then20:                                        ; preds = %if.test19
  %ic.deref22 = load i32, i32* %ptr, align 4
  %23 = add i32 %ic.deref22, 1
  store i32 %23, i32* %ptr, align 4
  br label %if.cont18

if.test21:                                        ; preds = %if.test19
  %24 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type25 = extractvalue %_Instruction %24, 0
  %25 = icmp eq i32 %s.field.type25, 2
  br i1 %25, label %if.then23, label %if.test24

if.then23:                                        ; preds = %if.test21
  %ic.deref26 = load i32, i32* %ptr, align 4
  %26 = sub i32 %ic.deref26, 1
  store i32 %26, i32* %ptr, align 4
  br label %if.cont18

if.test24:                                        ; preds = %if.test21
  %27 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type29 = extractvalue %_Instruction %27, 0
  %28 = icmp eq i32 %s.field.type29, 3
  br i1 %28, label %if.then27, label %if.test28

if.then27:                                        ; preds = %if.test24
  %ic.deref30 = load i32, i32* %ptr, align 4
  %ic.int31 = sext i32 %ic.deref30 to i64
  %ic.deref32 = load i32, i32* %ptr, align 4
  %ic.int33 = sext i32 %ic.deref32 to i64
  %29 = call i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %memory, i64 %ic.int33)
  %ic.deref34 = load i8, i8* %29, align 1
  %30 = add i8 %ic.deref34, 1
  %31 = call i8 @"_Ym.[]=(U8[&,I64,U8)U8"({ i8*, i64 }* %memory, i64 %ic.int31, i8 %30)
  br label %if.cont18

if.test28:                                        ; preds = %if.test24
  %32 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type37 = extractvalue %_Instruction %32, 0
  %33 = icmp eq i32 %s.field.type37, 4
  br i1 %33, label %if.then35, label %if.test36

if.then35:                                        ; preds = %if.test28
  %ic.deref38 = load i32, i32* %ptr, align 4
  %ic.int39 = sext i32 %ic.deref38 to i64
  %ic.deref40 = load i32, i32* %ptr, align 4
  %ic.int41 = sext i32 %ic.deref40 to i64
  %34 = call i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %memory, i64 %ic.int41)
  %ic.deref42 = load i8, i8* %34, align 1
  %35 = sub i8 %ic.deref42, 1
  %36 = call i8 @"_Ym.[]=(U8[&,I64,U8)U8"({ i8*, i64 }* %memory, i64 %ic.int39, i8 %35)
  br label %if.cont18

if.test36:                                        ; preds = %if.test28
  %37 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type45 = extractvalue %_Instruction %37, 0
  %38 = icmp eq i32 %s.field.type45, 5
  br i1 %38, label %if.then43, label %if.test44

if.then43:                                        ; preds = %if.test36
  %ic.deref46 = load i32, i32* %ptr, align 4
  %ic.int47 = sext i32 %ic.deref46 to i64
  %39 = call i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %memory, i64 %ic.int47)
  %c.nmut.deref48 = load i8, i8* %39, align 1
  %40 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str.2, i32 0, i32 0), i8 %c.nmut.deref48)
  br label %if.cont18

if.test44:                                        ; preds = %if.test36
  %41 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type51 = extractvalue %_Instruction %41, 0
  %42 = icmp eq i32 %s.field.type51, 7
  br i1 %42, label %if.then49, label %if.test50

if.then49:                                        ; preds = %if.test44
  br label %if.test53

if.test50:                                        ; preds = %if.test44
  %43 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.type61 = extractvalue %_Instruction %43, 0
  %44 = icmp eq i32 %s.field.type61, 8
  br i1 %44, label %if.then59, label %if.else

if.then59:                                        ; preds = %if.test50
  br label %if.test63

if.else:                                          ; preds = %if.test50
  ret i32 2

if.cont18:                                        ; preds = %if.cont62, %if.cont52, %if.then43, %if.then35, %if.then27, %if.then23, %if.then20
  %ic.deref70 = load i32, i32* %pc, align 4
  %45 = add i32 %ic.deref70, 1
  store i32 %45, i32* %pc, align 4
  br label %while.test

if.test53:                                        ; preds = %if.then49
  %ic.deref56 = load i32, i32* %ptr, align 4
  %ic.int57 = sext i32 %ic.deref56 to i64
  %46 = call i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %memory, i64 %ic.int57)
  %ic.deref58 = load i8, i8* %46, align 1
  %47 = icmp eq i8 %ic.deref58, 0
  br i1 %47, label %if.then54, label %if.test55

if.then54:                                        ; preds = %if.test53
  %48 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.payload = extractvalue %_Instruction %48, 1
  store i32 %s.field.payload, i32* %pc, align 4
  br label %if.cont52

if.test55:                                        ; preds = %if.test53
  br label %if.cont52

if.cont52:                                        ; preds = %if.test55, %if.then54
  br label %if.cont18

if.test63:                                        ; preds = %if.then59
  %ic.deref66 = load i32, i32* %ptr, align 4
  %ic.int67 = sext i32 %ic.deref66 to i64
  %49 = call i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %memory, i64 %ic.int67)
  %ic.deref68 = load i8, i8* %49, align 1
  %50 = icmp ne i8 %ic.deref68, 0
  br i1 %50, label %if.then64, label %if.test65

if.then64:                                        ; preds = %if.test63
  %51 = load %_Instruction, %_Instruction* %instr, align 4
  %s.field.payload69 = extractvalue %_Instruction %51, 1
  store i32 %s.field.payload69, i32* %pc, align 4
  br label %if.cont62

if.test65:                                        ; preds = %if.test63
  br label %if.cont62

if.cont62:                                        ; preds = %if.test65, %if.then64
  br label %if.cont18
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

define internal %_Instruction* @"_Ym.[](Instruction[&,I64)Instruction&"({ %_Instruction*, i64 }* %arg.slice, i64 %arg.offset) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %ic.deref = load { %_Instruction*, i64 }, { %_Instruction*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { %_Instruction*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %p.get_at.gep = getelementptr %_Instruction, %_Instruction* %sl.ptr.x, i64 %0
  ret %_Instruction* %p.get_at.gep
}

define internal i8* @"_Ym.[](U8[&,I64)U8&"({ i8*, i64 }* %arg.slice, i64 %arg.offset) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %ic.deref = load { i8*, i64 }, { i8*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %p.get_at.gep = getelementptr i8, i8* %sl.ptr.x, i64 %0
  ret i8* %p.get_at.gep
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

define internal i64 @"_Ym.c_len(U8*)I64"(i8* %arg.ptr) {
decl:
  %terminated = alloca i1, align 1
  %idx = alloca i64, align 8
  %chr = alloca i8, align 1
  br label %entry

entry:                                            ; preds = %decl
  %ptr = alloca i8*, align 8
  store i8* %arg.ptr, i8** %ptr, align 8
  store i1 false, i1* %terminated, align 1
  store i64 0, i64* %idx, align 8
  br label %while.test

while.test:                                       ; preds = %if.cont, %entry
  %ic.deref = load i1, i1* %terminated, align 1
  %0 = call i1 @"_Ym.!(Bool)Bool"(i1 %ic.deref)
  br i1 %0, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %1 = load i8*, i8** %ptr, align 8
  %ic.deref1 = load i64, i64* %idx, align 8
  %p.get_at.gep = getelementptr i8, i8* %1, i64 %ic.deref1
  %c.nmut.deref = load i8, i8* %p.get_at.gep, align 1
  store i8 %c.nmut.deref, i8* %chr, align 1
  br label %if.test

while.merge:                                      ; preds = %while.test
  %2 = load i64, i64* %idx, align 8
  ret i64 %2

if.test:                                          ; preds = %while.head
  %ic.deref3 = load i8, i8* %chr, align 1
  %3 = icmp eq i8 %ic.deref3, 0
  br i1 %3, label %if.then, label %if.else

if.then:                                          ; preds = %if.test
  store i1 true, i1* %terminated, align 1
  br label %if.cont

if.else:                                          ; preds = %if.test
  %ic.deref4 = load i64, i64* %idx, align 8
  %4 = add i64 %ic.deref4, 1
  store i64 %4, i64* %idx, align 8
  br label %if.cont

if.cont:                                          ; preds = %if.else, %if.then
  br label %while.test
}

declare noalias i8* @malloc(i32)

define internal { %_Instruction*, i64 } @"_Ym.parse(U8[)Instruction["({ i8*, i64 } %arg.input) {
decl:
  %i = alloca i32, align 4
  %instructions = alloca { %_Instruction*, i64 }, align 8
  %sl.ctor.definit.iter = alloca i64, align 8
  %stack = alloca { i32*, i64 }, align 8
  %sl.ctor.definit.iter3 = alloca i64, align 8
  %stack_i = alloca i32, align 4
  %chr = alloca i8, align 1
  %instruction = alloca %_Instruction, align 8
  %jump_target = alloca i32, align 4
  br label %entry

entry:                                            ; preds = %decl
  %input = alloca { i8*, i64 }, align 8
  store { i8*, i64 } %arg.input, { i8*, i64 }* %input, align 8
  store i32 0, i32* %i, align 4
  %0 = load { i8*, i64 }, { i8*, i64 }* %input, align 8
  %1 = extractvalue { i8*, i64 } %0, 1
  %sl.ctor.size = trunc i64 %1 to i32
  %mallocsize = mul i32 %sl.ctor.size, ptrtoint (%_Instruction* getelementptr (%_Instruction, %_Instruction* null, i32 1) to i32)
  %malloccall = tail call i8* @malloc(i32 %mallocsize)
  %2 = bitcast i8* %malloccall to %_Instruction*
  %3 = insertvalue { %_Instruction*, i64 } undef, %_Instruction* %2, 0
  %sl.ctor.inst = insertvalue { %_Instruction*, i64 } %3, i64 %1, 1
  store i64 0, i64* %sl.ctor.definit.iter, align 8
  br label %sl.ctor.definit.test

sl.ctor.definit.test:                             ; preds = %sl.ctor.definit.head, %entry
  %4 = load i64, i64* %sl.ctor.definit.iter, align 8
  %sl.ctor.definit.cmp = icmp slt i64 %4, %1
  br i1 %sl.ctor.definit.cmp, label %sl.ctor.definit.head, label %sl.ctor.definit.merge

sl.ctor.definit.head:                             ; preds = %sl.ctor.definit.test
  %5 = load i64, i64* %sl.ctor.definit.iter, align 8
  %sl.ctor.definit.gep = getelementptr inbounds %_Instruction, %_Instruction* %2, i64 %5
  store %_Instruction zeroinitializer, %_Instruction* %sl.ctor.definit.gep, align 4
  %6 = load i64, i64* %sl.ctor.definit.iter, align 8
  %7 = add i64 %6, 1
  store i64 %7, i64* %sl.ctor.definit.iter, align 8
  br label %sl.ctor.definit.test

sl.ctor.definit.merge:                            ; preds = %sl.ctor.definit.test
  store { %_Instruction*, i64 } %sl.ctor.inst, { %_Instruction*, i64 }* %instructions, align 8
  %malloccall1 = tail call i8* @malloc(i32 mul (i32 ptrtoint (i32* getelementptr (i32, i32* null, i32 1) to i32), i32 512))
  %8 = bitcast i8* %malloccall1 to i32*
  %9 = insertvalue { i32*, i64 } undef, i32* %8, 0
  %sl.ctor.inst2 = insertvalue { i32*, i64 } %9, i64 512, 1
  store i64 0, i64* %sl.ctor.definit.iter3, align 8
  br label %sl.ctor.definit.test4

sl.ctor.definit.test4:                            ; preds = %sl.ctor.definit.head5, %sl.ctor.definit.merge
  %10 = load i64, i64* %sl.ctor.definit.iter3, align 8
  %sl.ctor.definit.cmp7 = icmp slt i64 %10, 512
  br i1 %sl.ctor.definit.cmp7, label %sl.ctor.definit.head5, label %sl.ctor.definit.merge6

sl.ctor.definit.head5:                            ; preds = %sl.ctor.definit.test4
  %11 = load i64, i64* %sl.ctor.definit.iter3, align 8
  %sl.ctor.definit.gep8 = getelementptr inbounds i32, i32* %8, i64 %11
  store i32 0, i32* %sl.ctor.definit.gep8, align 4
  %12 = load i64, i64* %sl.ctor.definit.iter3, align 8
  %13 = add i64 %12, 1
  store i64 %13, i64* %sl.ctor.definit.iter3, align 8
  br label %sl.ctor.definit.test4

sl.ctor.definit.merge6:                           ; preds = %sl.ctor.definit.test4
  store { i32*, i64 } %sl.ctor.inst2, { i32*, i64 }* %stack, align 8
  store i32 0, i32* %stack_i, align 4
  br label %while.test

while.test:                                       ; preds = %if.cont, %sl.ctor.definit.merge6
  %ic.deref = load i32, i32* %i, align 4
  %ic.int = sext i32 %ic.deref to i64
  %14 = load { i8*, i64 }, { i8*, i64 }* %input, align 8
  %15 = extractvalue { i8*, i64 } %14, 1
  %16 = icmp slt i64 %ic.int, %15
  br i1 %16, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %17 = load { i8*, i64 }, { i8*, i64 }* %input, align 8
  %ic.deref9 = load i32, i32* %i, align 4
  %ic.int10 = sext i32 %ic.deref9 to i64
  %18 = call i8 @"_Ym.[](U8[,I64)U8"({ i8*, i64 } %17, i64 %ic.int10)
  store i8 %18, i8* %chr, align 1
  store %_Instruction zeroinitializer, %_Instruction* %instruction, align 4
  br label %if.test

while.merge:                                      ; preds = %while.test
  %19 = load { %_Instruction*, i64 }, { %_Instruction*, i64 }* %instructions, align 8
  ret { %_Instruction*, i64 } %19

if.test:                                          ; preds = %while.head
  %ic.deref12 = load i8, i8* %chr, align 1
  %20 = icmp eq i8 %ic.deref12, 62
  br i1 %20, label %if.then, label %if.test11

if.then:                                          ; preds = %if.test
  store %_Instruction { i32 1, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test11:                                        ; preds = %if.test
  %ic.deref15 = load i8, i8* %chr, align 1
  %21 = icmp eq i8 %ic.deref15, 60
  br i1 %21, label %if.then13, label %if.test14

if.then13:                                        ; preds = %if.test11
  store %_Instruction { i32 2, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test14:                                        ; preds = %if.test11
  %ic.deref18 = load i8, i8* %chr, align 1
  %22 = icmp eq i8 %ic.deref18, 43
  br i1 %22, label %if.then16, label %if.test17

if.then16:                                        ; preds = %if.test14
  store %_Instruction { i32 3, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test17:                                        ; preds = %if.test14
  %ic.deref21 = load i8, i8* %chr, align 1
  %23 = icmp eq i8 %ic.deref21, 45
  br i1 %23, label %if.then19, label %if.test20

if.then19:                                        ; preds = %if.test17
  store %_Instruction { i32 4, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test20:                                        ; preds = %if.test17
  %ic.deref24 = load i8, i8* %chr, align 1
  %24 = icmp eq i8 %ic.deref24, 46
  br i1 %24, label %if.then22, label %if.test23

if.then22:                                        ; preds = %if.test20
  store %_Instruction { i32 5, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test23:                                        ; preds = %if.test20
  %ic.deref27 = load i8, i8* %chr, align 1
  %25 = icmp eq i8 %ic.deref27, 44
  br i1 %25, label %if.then25, label %if.test26

if.then25:                                        ; preds = %if.test23
  store %_Instruction { i32 6, i32 0 }, %_Instruction* %instruction, align 4
  br label %if.cont

if.test26:                                        ; preds = %if.test23
  %ic.deref30 = load i8, i8* %chr, align 1
  %26 = icmp eq i8 %ic.deref30, 91
  br i1 %26, label %if.then28, label %if.test29

if.then28:                                        ; preds = %if.test26
  %27 = call i32 @"_Ym.-(I32)I32"(i32 1)
  %s.ctor.wf.payload = insertvalue %_Instruction { i32 7, i32 undef }, i32 %27, 1
  store %_Instruction %s.ctor.wf.payload, %_Instruction* %instruction, align 4
  %ic.deref31 = load i32, i32* %stack_i, align 4
  %ic.int32 = sext i32 %ic.deref31 to i64
  %ic.deref33 = load i32, i32* %i, align 4
  %28 = call i32 @"_Ym.[]=(I32[&,I64,I32)I32"({ i32*, i64 }* %stack, i64 %ic.int32, i32 %ic.deref33)
  %ic.deref34 = load i32, i32* %stack_i, align 4
  %29 = add i32 %ic.deref34, 1
  store i32 %29, i32* %stack_i, align 4
  br label %if.cont

if.test29:                                        ; preds = %if.test26
  %ic.deref37 = load i8, i8* %chr, align 1
  %30 = icmp eq i8 %ic.deref37, 93
  br i1 %30, label %if.then35, label %if.test36

if.then35:                                        ; preds = %if.test29
  %ic.deref38 = load i32, i32* %stack_i, align 4
  %31 = sub i32 %ic.deref38, 1
  store i32 %31, i32* %stack_i, align 4
  %ic.deref39 = load i32, i32* %stack_i, align 4
  %ic.int40 = sext i32 %ic.deref39 to i64
  %32 = call i32* @"_Ym.[](I32[&,I64)I32&"({ i32*, i64 }* %stack, i64 %ic.int40)
  %c.nmut.deref = load i32, i32* %32, align 4
  store i32 %c.nmut.deref, i32* %jump_target, align 4
  %33 = load i32, i32* %jump_target, align 4
  %s.ctor.wf.payload41 = insertvalue %_Instruction { i32 8, i32 undef }, i32 %33, 1
  store %_Instruction %s.ctor.wf.payload41, %_Instruction* %instruction, align 4
  %ic.deref42 = load i32, i32* %jump_target, align 4
  %ic.int43 = sext i32 %ic.deref42 to i64
  %34 = call %_Instruction* @"_Ym.[](Instruction[&,I64)Instruction&.1"({ %_Instruction*, i64 }* %instructions, i64 %ic.int43)
  %35 = load i32, i32* %i, align 4
  %s.sf.payload = getelementptr inbounds %_Instruction, %_Instruction* %34, i32 0, i32 1
  store i32 %35, i32* %s.sf.payload, align 4
  br label %if.cont

if.test36:                                        ; preds = %if.test29
  br label %if.cont

if.cont:                                          ; preds = %if.test36, %if.then35, %if.then28, %if.then25, %if.then22, %if.then19, %if.then16, %if.then13, %if.then
  %ic.deref44 = load i32, i32* %i, align 4
  %ic.int45 = sext i32 %ic.deref44 to i64
  %ic.deref46 = load %_Instruction, %_Instruction* %instruction, align 4
  %36 = call %_Instruction @"_Ym.[]=(Instruction[&,I64,Instruction)Instruction"({ %_Instruction*, i64 }* %instructions, i64 %ic.int45, %_Instruction %ic.deref46)
  %ic.deref47 = load i32, i32* %i, align 4
  %37 = add i32 %ic.deref47, 1
  store i32 %37, i32* %i, align 4
  br label %while.test
}

define internal i8 @"_Ym.[](U8[,I64)U8"({ i8*, i64 } %arg.slice, i64 %arg.offset) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %slice = alloca { i8*, i64 }, align 8
  store { i8*, i64 } %arg.slice, { i8*, i64 }* %slice, align 8
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %0 = load { i8*, i64 }, { i8*, i64 }* %slice, align 8
  %sl.ptr.x = extractvalue { i8*, i64 } %0, 0
  %1 = load i64, i64* %offset, align 8
  %p.get_at.gep = getelementptr i8, i8* %sl.ptr.x, i64 %1
  %c.nmut.deref = load i8, i8* %p.get_at.gep, align 1
  ret i8 %c.nmut.deref
}

define internal i32 @"_Ym.[]=(I32[&,I64,I32)I32"({ i32*, i64 }* %arg.slice, i64 %arg.offset, i32 %arg.val) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %val = alloca i32, align 4
  store i32 %arg.val, i32* %val, align 4
  %ic.deref = load { i32*, i64 }, { i32*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { i32*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %1 = load i32, i32* %val, align 4
  %p.set_at.gep = getelementptr i32, i32* %sl.ptr.x, i64 %0
  store i32 %1, i32* %p.set_at.gep, align 4
  %2 = load i32, i32* %val, align 4
  ret i32 %2
}

define internal i32* @"_Ym.[](I32[&,I64)I32&"({ i32*, i64 }* %arg.slice, i64 %arg.offset) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %ic.deref = load { i32*, i64 }, { i32*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { i32*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %p.get_at.gep = getelementptr i32, i32* %sl.ptr.x, i64 %0
  ret i32* %p.get_at.gep
}

define internal %_Instruction* @"_Ym.[](Instruction[&,I64)Instruction&.1"({ %_Instruction*, i64 }* %arg.slice, i64 %arg.offset) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %ic.deref = load { %_Instruction*, i64 }, { %_Instruction*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { %_Instruction*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %p.get_at.gep = getelementptr %_Instruction, %_Instruction* %sl.ptr.x, i64 %0
  ret %_Instruction* %p.get_at.gep
}

define internal %_Instruction @"_Ym.[]=(Instruction[&,I64,Instruction)Instruction"({ %_Instruction*, i64 }* %arg.slice, i64 %arg.offset, %_Instruction %arg.val) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %offset = alloca i64, align 8
  store i64 %arg.offset, i64* %offset, align 8
  %val = alloca %_Instruction, align 8
  store %_Instruction %arg.val, %_Instruction* %val, align 4
  %ic.deref = load { %_Instruction*, i64 }, { %_Instruction*, i64 }* %arg.slice, align 8
  %sl.ptr.x = extractvalue { %_Instruction*, i64 } %ic.deref, 0
  %0 = load i64, i64* %offset, align 8
  %1 = load %_Instruction, %_Instruction* %val, align 4
  %p.set_at.gep = getelementptr %_Instruction, %_Instruction* %sl.ptr.x, i64 %0
  store %_Instruction %1, %_Instruction* %p.set_at.gep, align 4
  %2 = load %_Instruction, %_Instruction* %val, align 4
  ret %_Instruction %2
}

declare i32 @printf(i8*, ...)

define internal i1 @"_Ym.!(Bool)Bool"(i1 %arg.a) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i1, align 1
  store i1 %arg.a, i1* %a, align 1
  br label %if.test

if.test:                                          ; preds = %entry
  %0 = load i1, i1* %a, align 1
  br i1 %0, label %if.then, label %if.else

if.then:                                          ; preds = %if.test
  ret i1 false

if.else:                                          ; preds = %if.test
  ret i1 true
}

define internal i32 @"_Ym.-(I32)I32"(i32 %arg.a) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca i32, align 4
  store i32 %arg.a, i32* %a, align 4
  %0 = load i32, i32* %a, align 4
  %1 = sub i32 0, %0
  ret i32 %1
}
