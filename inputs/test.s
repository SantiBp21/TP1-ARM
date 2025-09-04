.text
movz x1, 5      
movz x2, 10     
movz x3, 0      
add x4, x1, x2  
add x5, x1, 7   
mul x6, x1, x2
cbz x3, salto1
movz x7, 99
b continuar

salto1:
movz x7, 42

continuar:
cbnz x3, salto2
movz x8, 42
b fin

salto2:
movz x8, 99

fin:
HLT 0

