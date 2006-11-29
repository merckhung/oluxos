	.file	"console.c"
	.section	.rodata
.LC0:
	.string	"Hello world!"
	.text
.globl ia32_printf
	.type	ia32_printf, @function





ia32_printf:


    |
    | Save EBP, move to new EBP
    |
	pushl	%ebp
	movl	%esp, %ebp


    |
    | Save EDI
    |
	pushl	%edi



    |
    | ESP - 36
    |
	subl	$36, %esp



    | *(EBP - 24) = 0xb800
	movl	$753664, -24(%ebp)


    | *(EBP - 20) = String pointer
	movl	$.LC0, -20(%ebp)


    | EAX = string pointer
	movl	-20(%ebp), %eax


    | ECX = -1
	movl	$-1, %ecx


    | EBP = EAX = string pointer
	movl	%eax, -40(%ebp)


    | AL = 0
	movb	$0, %al

	cld
	movl	-40(%ebp), %edi
	repnz
	scasb





	movl	%ecx, %eax
	notl	%eax
	decl	%eax
	movl	%eax, -16(%ebp)
	movl	$0, -12(%ebp)
	movl	$0, -8(%ebp)
	jmp	.L2
.L3:
	movl	-12(%ebp), %eax
	movl	%eax, %edx
	addl	-24(%ebp), %edx
	movl	-8(%ebp), %eax
	addl	-20(%ebp), %eax
	movzbl	(%eax), %eax
	movb	%al, (%edx)
	addl	$2, -12(%ebp)
	incl	-8(%ebp)
.L2:
	movl	-8(%ebp), %eax
	cmpl	-16(%ebp), %eax
	jl	.L3
	addl	$36, %esp
	popl	%edi


    | Restore EBP
	popl	%ebp
	ret



	.size	ia32_printf, .-ia32_printf









.globl ia32_cls
	.type	ia32_cls, @function



ia32_cls:

    | Save EBP
	pushl	%ebp


    | EBP = ESP, new stack pointer
	movl	%esp, %ebp


    | Restire EBP
	popl	%ebp


	ret


	.size	ia32_cls, .-ia32_cls




	.ident	"GCC: (GNU) 4.1.1 (Gentoo 4.1.1-r1)"
	.section	.note.GNU-stack,"",@progbits
