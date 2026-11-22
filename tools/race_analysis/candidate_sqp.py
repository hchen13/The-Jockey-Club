"""Independent geometric SQP experiments, not an implementation of TUM IQP.
Run in a bounded, single-threaded child; final native verification is mandatory.
"""
from pathlib import Path
import os,sys,json,time
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import numpy as np
import quadprog

def evaluate(x,xy,normal):
 q=xy+x[:,None]*normal
 a=q[1:-1]-q[:-2];b=q[2:]-q[1:-1];c=a+b
 aa=(a*a).sum(axis=1);bb=(b*b).sum(axis=1);cc=(c*c).sum(axis=1)
 den=np.sqrt(np.maximum(aa*bb*cc,1e-16));k=2*(a[:,0]*b[:,1]-a[:,1]*b[:,0])/den
 J=np.zeros((len(x)-2,len(x)))
 for dj,du,dv in ((0,-1,0),(1,1,-1),(2,0,1)):
  nm=normal[dj:len(x)-2+dj];da=nm*du;db=nm*dv
  numerator=2*(da[:,0]*b[:,1]-da[:,1]*b[:,0]+a[:,0]*db[:,1]-a[:,1]*db[:,0])
  logden=(a*da).sum(axis=1)/np.maximum(aa,1e-8)+(b*db).sum(axis=1)/np.maximum(bb,1e-8)+(c*(da+db)).sum(axis=1)/np.maximum(cc,1e-8)
  J[np.arange(len(x)-2),np.arange(len(x)-2)+dj]=numerator/den-k*logden
 return k,J,(np.sqrt(aa)+np.sqrt(bb))/2

def solve(source, destination, power):
 d=json.loads(Path(source).read_text());nodes=np.array(d['nodes']);s=nodes[:,0];xy=nodes[:,1:3];normal=nodes[:,3:5];lo=nodes[:,5];hi=nodes[:,6];x=nodes[:,7].copy();N=len(x)
 if N>1800:raise ValueError(f'SQP node budget exceeded: {N} > 1800; K1999 remains available')
 cs=np.array(d['constraints']);A=np.zeros((len(cs),N))
 for j,c in enumerate(cs):A[j,int(c[0])]+=c[2];A[j,int(c[1])]+=c[3]
 lb=cs[:,4];ub=cs[:,5];ids=np.flatnonzero(hi-lo>1e-8);pinned=np.flatnonzero(hi-lo<=1e-8);x[pinned]=lo[pinned]
 fixed=A[:,pinned]@x[pinned];Af=A[:,ids];ok=np.any(abs(Af)>1e-12,axis=1)
 Af=Af[ok];ll=(lb-fixed)[ok];uu=(ub-fixed)[ok]
 # The full sparse section coupling is retained in every quadratic solve.
 C=np.concatenate((Af,-Af),axis=0).T
 rhs=np.r_[ll,-uu]
 Mf=len(ids);eye=np.eye(Mf);C=np.concatenate((C,eye,-eye),axis=1)
 records=[];t=time.perf_counter();stop='iteration-budget'
 if not Mf:stop='fixed-corridor'
 for it in range(30 if Mf else 0):
  k,J,mass=evaluate(x,xy,normal);J=J[:,ids]
  weights=mass if power==2 else mass*np.maximum(k*k,1e-6)
  H=J.T@(weights[:,None]*J)+eye*1e-7
  b=H@x[ids]-J.T@(weights*k)
  trust=2.0
  lbx=np.maximum(lo[ids],x[ids]-trust);ubx=np.minimum(hi[ids],x[ids]+trust)
  try:sol=quadprog.solve_qp(H,b,C,np.r_[rhs,lbx,-ubx])[0]
  except ValueError as e:raise ValueError(f'QP constraints rejected at iteration {it}: {e}') from e
  old=x.copy();score=float(np.dot(mass,k**power));accepted=False
  for fraction in [1,.5,.25,.125,.0625,.03125]:
   x[ids]=old[ids]+fraction*(sol-old[ids]);kk,_,mm=evaluate(x,xy,normal)
   # Actual geometric score, not estimated speed or traversal time.
   value=float(np.dot(mm,kk**power))
   if value<score-1e-11:accepted=True;break
  if not accepted:x=old;stop='line-search-stopped';break
  movement=float(np.max(abs(x-old)));records.append({'iteration':it,'objective':value,'movement':movement})
  if movement<1e-4:stop='movement-tolerance';break
 path=Path(destination);np.savetxt(path,np.c_[s,x],fmt='%.12f')
 violation=float(max(0,np.max(A@x-ub,initial=0),np.max(lb-A@x,initial=0)))
 if violation>1e-4:raise ValueError(f'SQP seed violates linear constraints: {violation}')
 path.with_suffix('.json').write_text(json.dumps({'solve_ms':(time.perf_counter()-t)*1000,'iterations':len(records),'stop':stop,'linear_constraint_violation':violation}),encoding='utf8')

if __name__=='__main__':
 import argparse
 cli=argparse.ArgumentParser()
 cli.add_argument('source');cli.add_argument('destination');cli.add_argument('--power',type=int,choices=(2,4),required=True)
 args=cli.parse_args()
 solve(args.source,args.destination,args.power)
