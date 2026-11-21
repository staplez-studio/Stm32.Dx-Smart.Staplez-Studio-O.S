import re,sys
p=sys.argv[1]+'/Drivers/CMSIS/Include/core_cm3.h'
t=open(p).read()
t=re.sub(r'#define DWT\s+\(\(DWT_Type\s*\*\)\s*DWT_BASE\s*\)','#define DWT host_dwt()\nstatic inline DWT_Type *host_dwt(void){ static DWT_Type d; d.CYCCNT += 400; return &d; }',t)
open(p,'w').write(t)
