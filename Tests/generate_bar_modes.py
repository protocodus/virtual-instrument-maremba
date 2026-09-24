#!/usr/bin/env python3
"""Generate/check reduced modes of three voiced, smoothly undercut wooden bars.

Offline only: numpy/scipy. Consistent-mass cubic Hermite Euler--Bernoulli FEM,
free/free boundary conditions. Positive symmetric thickness is fitted to the
first three existing family ratios, not measured specimen geometry. The same
beam gives every spatial shape and all eight frequency ratios. The derived
functions are tabulated on half of the bar; exact parity supplies the rest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from scipy.optimize import least_squares
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigsh

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'DSP' / 'BarModes.h'
MODES = 8
# Original family targets for the first three flexural modes only.
TARGETS = ((4., 10.), (4., 9.8), (4., 9.6))
NAMES = ('Rosewood', 'Padauk', 'Balafon')
DEPTH_STEPS = 17
POINTS = 65                 # x=0 ... 0.5, cubic displacement/slope interpolation
# Dimensionless shape constants are voiced, not measured wood constants.
# The shape is 1 - d * (1 - |(2x-1)/a|^p)^2 inside |2x-1| < a.
# The first derivative of thickness is continuous at the ends of the cut.


def thickness(x, depth, extent, power):
    z = abs((2*x - 1) / extent)
    return 1 - depth * np.maximum(1-z**power, 0)**2


def solve(depth, extent, power, elements):
    size = 2*elements+2
    K, M = np.zeros((size,size)), np.zeros((size,size))
    h = 1/elements
    gauss, weights = np.polynomial.legendre.leggauss(4)
    for element in range(elements):
        ke, me = np.zeros((4,4)), np.zeros((4,4))
        for z, weight in zip((gauss+1)/2, weights/2):
            n = np.array([1-3*z*z+2*z**3, h*(z-2*z*z+z**3),
                          3*z*z-2*z**3, h*(-z*z+z**3)])
            d2 = np.array([-6+12*z, h*(-4+6*z), 6-12*z, h*(-2+6*z)])/h**2
            t = thickness((element+z)*h, depth, extent, power)
            ke += weight*h*t**3*np.outer(d2,d2)
            me += weight*h*t*np.outer(n,n)
        sl = slice(2*element,2*element+4)
        K[sl,sl] += ke
        M[sl,sl] += me
    eig, vec = eigsh(csr_matrix(K), k=10, M=csr_matrix(M), sigma=-1.,
                    which='LM', v0=np.linspace(1.,2.,size), tol=1e-12)
    order = np.argsort(eig)
    eig, vec = eig[order][2:], vec[:,order][:,2:]
    assert np.all(eig > 0)
    gram=vec.T@M@vec
    assert np.max(np.abs(gram-np.eye(MODES))) < 2e-9, 'modal mass orthogonality'
    mirrored=vec[::2][::-1]
    for mode in range(MODES):
        parity=-1 if mode%2 else 1
        assert np.max(np.abs(vec[::2,mode]-parity*mirrored[:,mode])) < 3e-5, 'mode parity/tracking'
    # Match the existing free-beam modal orientation and normalization at the
    # uniform limit. sqrt(total mass) makes normalization independent of mass
    # units; thin centre regions then have the mobility of this actual beam.
    mass = np.sum(M[::2,::2])
    x = np.linspace(0,1,elements+1)
    for mode in range(MODES):
        orientation = -1. if (mode//2)%2 == 0 else 1.
        if np.sign(vec[0,mode]) != np.sign(orientation):
            vec[:,mode] *= -1
        vec[:,mode] *= np.sqrt(mass) * (.82260894024917 if mode == 0 else .70710678118655)
    return np.sqrt(eig), vec, M, K


def fit():
    results=[]
    for name, target, power in zip(NAMES,TARGETS,(3.3,3.,2.8)):
        def error(parameters):
            omega, *_ = solve(*parameters,power,128)
            return np.log(omega[1:3]/omega[0]/target)
        result=least_squares(error, [.70,.79], bounds=([.10,.25],[.88,.99]),
                             xtol=1e-11, ftol=1e-11, gtol=1e-10, diff_step=1e-4)
        assert np.max(np.abs(result.fun)) < 1e-5, (name,result.x,result.fun)
        results.append((*result.x,power))
        print(name, 'fit', [format(v,'.17g') for v in (*result.x,power)], 'relative error', np.exp(result.fun)-1)
    return results


def sample(vec, elements, x):
    x=np.asarray(x)
    e=np.minimum((x*elements).astype(int),elements-1)
    z=x*elements-e
    h=1/elements
    n=np.array([1-3*z*z+2*z**3,h*(z-2*z*z+z**3),3*z*z-2*z**3,h*(-z*z+z**3)])
    dn=np.array([-6*z+6*z*z,h*(1-4*z+3*z*z),6*z-6*z*z,h*(-2*z+3*z*z)])/h
    values=sum(n[i,:,None]*vec[2*e+i,:] for i in range(4))
    slopes=sum(dn[i,:,None]*vec[2*e+i,:] for i in range(4))
    return values,slopes


def tables(parameters):
    rows=[]
    diagnostics=[]
    for name,(depth,extent,power) in zip(NAMES,parameters):
        family=[]
        for index in range(DEPTH_STEPS):
            d=depth*(1-index/(DEPTH_STEPS-1))
            omega, vec, M, K=solve(d,extent,power,128)
            fine, fine_vec, *_ =solve(d,extent,power,256)
            ratio=omega/omega[0]
            cents=np.max(np.abs(1200*np.log2((fine/fine[0])/ratio)))
            assert cents < .1, (name,index,cents)
            x=np.linspace(0,.5,POINTS)
            values, slopes=sample(vec,128,x)
            fv,fs=sample(fine_vec,256,x)
            shape_error=np.max(np.abs(values-fv))
            assert shape_error < 3e-4, (name,index,shape_error)
            # Explicitly impose exact endpoint parity before serialization.
            values[-1,1::2]=0
            slopes[-1,::2]=0
            # Calculate physical fundamental node for the documentation/tests.
            full_x=np.linspace(0,.5,8193)
            full_y=sample(vec,128,full_x)[0][:,0]
            j=np.flatnonzero(full_y[:-1]*full_y[1:]<0)[0]
            node=np.interp(0,full_y[j:j+2],full_x[j:j+2])
            if index < DEPTH_STEPS-1:
                mid_depth=depth*(1-(index+.5)/(DEPTH_STEPS-1))
                mid_w,mid_v,*_=solve(mid_depth,extent,power,128)
                next_w,next_v,*_=solve(depth*(1-(index+1)/(DEPTH_STEPS-1)),extent,power,128)
                frequency_interpolation=np.max(np.abs(1200*np.log2(.5*(ratio+next_w/next_w[0])/(mid_w/mid_w[0]))))
                next_shape=sample(next_v,128,x)[0]
                middle_shape=sample(mid_v,128,x)[0]
                shape_interpolation=np.max(np.abs(.5*(values+next_shape)-middle_shape))
                assert frequency_interpolation < 6., 'depth frequency interpolation too coarse'
                assert shape_interpolation < .01, 'depth shape interpolation too coarse'
            else:
                frequency_interpolation=shape_interpolation=0.
            family.append((ratio,values,slopes,node))
            diagnostics.append(dict(family=name,depth_index=index,cut_depth=float(d),extent=float(extent),power=float(power),
                ratios=ratio.tolist(),fundamental_node=float(node),
                max_convergence_cents=float(cents),max_shape_convergence=float(shape_error),
                midpoint_interpolation_cents=float(frequency_interpolation),
                midpoint_shape_interpolation=float(shape_interpolation)))
        rows.append(family)
    return rows,diagnostics


def emit(rows,parameters):
    f=lambda v: format(float(v),'.17g')
    out=['// BEGIN GENERATED BAR MODES',
         '// Generator SHA256: '+hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
         'static constexpr int kBarModePointCount = %d;'%POINTS,
         'static constexpr int kBarModeDepthCount = %d;'%DEPTH_STEPS,
         'struct BarModeTable { double ratio[8]; double node; double value[8][%d]; double slope[8][%d]; double integral[8][%d]; };'%(POINTS,POINTS,POINTS),
         'static constexpr BarModeTable kBarModeTables[3][kBarModeDepthCount] = {']
    for family, (depth,extent,power), name in zip(rows,parameters,NAMES):
        out.append('    { // '+name+' depth='+f(depth)+' extent='+f(extent)+' power='+f(power))
        for ratio,values,slopes,node in family:
            out.append('        {{'+', '.join(map(f,ratio))+'}, '+f(node)+', {')
            out.extend('            {'+', '.join(map(f,values[:,m]))+'},' for m in range(MODES))
            out.append('        }, {')
            out.extend('            {'+', '.join(map(f,slopes[:,m]))+'},' for m in range(MODES))
            out.append('        }, {')
            step=.5/(POINTS-1)
            y0,y1=values[:-1],values[1:]
            v0,v1=step*slopes[:-1],step*slopes[1:]
            a,b=2*(y0-y1)+v0+v1,3*(y1-y0)-2*v0-v1
            integrals=np.vstack([np.zeros(MODES),np.cumsum(step*(a/4+b/3+v0/2+y0),axis=0)])
            out.extend('            {'+', '.join(map(f,integrals[:,m]))+'},' for m in range(MODES))
            out.append('        }},')
        out.append('    },')
    out+=['};','// END GENERATED BAR MODES']
    return '\n'.join(out)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--write',action='store_true')
    parser.add_argument('--fit',action='store_true')
    parser.add_argument('--diagnostics',action='store_true',help='print full JSON diagnostics')
    args=parser.parse_args()
    # Refit from first principles with --fit. Frozen fits make fixture checks
    # deterministic and keep routine verification independent of optimizer path.
    parameters=fit() if args.fit else PARAMETERS
    rows,diagnostics=tables(parameters)
    generated=emit(rows,parameters)
    if args.diagnostics:
        print(json.dumps(diagnostics,indent=2))
    else:
        for field in ('max_convergence_cents','max_shape_convergence',
                      'midpoint_interpolation_cents','midpoint_shape_interpolation'):
            print(field+': '+format(max(row[field] for row in diagnostics),'.9g'))
    if args.write:
        source=HEADER.read_text()
        source=re.sub(r'// BEGIN GENERATED BAR MODES.*?// END GENERATED BAR MODES',
                      lambda _:generated,source,flags=re.S)
        HEADER.write_text(source)
    elif not args.fit:
        source=HEADER.read_text()
        # Floating eigenvectors may differ in the last few bits across BLAS
        # versions. Check generated numbers numerically and generator hash exactly.
        old=re.search(r'// BEGIN GENERATED BAR MODES.*?// END GENERATED BAR MODES',source,re.S).group()
        assert re.search(r'Generator SHA256: (\w+)',old).group(1)==hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        num=lambda s:np.array([float(v) for v in re.findall(r'(?<![A-Za-z_])[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?', re.sub(r'//[^\n]*','',s))])
        a,b=num(old),num(generated)
        assert a.shape==b.shape and np.allclose(a,b,rtol=2e-7,atol=2e-7), 'regenerate stale modal fixture'
        print('Bar mode fixture and generator SHA256 verified.')


PARAMETERS = ((.72571911682416879,.84005859974153141,3.3),
              (.62159288551128422,.6860334886321563,3.),
              (.58794124672254122,.64877794877134387,2.8))
if __name__=='__main__': main()
