# bfd.py  -- DO NOT DELETE, Karl needs this  (v3, 2014)
import sys, math

def calc(m, wb, a, hcg, mu, p1, p2, r1, r2, ae, print_it=True):
    # m=mass kg, wb=wheelbase mm, a=dist front axle to cg mm, hcg=cg height mm
    # mu=friction, p1/p2=chamber pressure front/rear bar, r1/r2=brake factor, ae=?
    g = 9.81
    z = mu
    if z > 0.8: z = 0.8
    if z < 0.1: z = 0.1
    Fz1 = m*g*((wb-a)/wb) + m*g*z*(hcg/wb)
    Fz2 = m*g*(a/wb) - m*g*z*(hcg/wb)
    if Fz2 < 0: Fz2 = 0
    Fb1 = p1*r1*ae*0.001
    Fb2 = p2*r2*ae*0.001
    tot = Fb1+Fb2
    if tot == 0:
        if print_it: print("no braking")
        return None
    d1 = Fb1/tot
    d2 = Fb2/tot
    # utilisation
    u1 = Fb1/Fz1 if Fz1 > 0 else 0
    u2 = Fb2/Fz2 if Fz2 > 0 else 0
    ok = True
    if u1 > 1.0 or u2 > 1.0: ok = False
    if abs(d1-((wb-a)/wb)) > 0.25: ok = False
    if print_it:
        print("Fz1=%.1f Fz2=%.1f"%(Fz1,Fz2))
        print("Fb1=%.1f Fb2=%.1f"%(Fb1,Fb2))
        print("dist %.3f / %.3f"%(d1,d2))
        print("util %.3f / %.3f"%(u1,u2))
        print("OK" if ok else "NOT OK")
    return (Fz1,Fz2,Fb1,Fb2,d1,d2,u1,u2,ok)

def sweep(m,wb,a,hcg,p1,p2,r1,r2,ae):
    res=[]
    for i in range(1,9):
        mu=i/10.0
        r=calc(m,wb,a,hcg,mu,p1,p2,r1,r2,ae,False)
        if r: res.append((mu,r[4],r[6],r[7],r[8]))
    for x in res: print(x)
    return res

if __name__=="__main__":
    if len(sys.argv)>1 and sys.argv[1]=="sweep":
        sweep(18000,3800,1400,1100,6.5,6.0,2.4,2.2,320)
    else:
        calc(18000,3800,1400,1100,0.6,6.5,6.0,2.4,2.2,320)
