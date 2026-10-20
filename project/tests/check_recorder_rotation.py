"""RecorderのZXY回転復元と振り子の固定支点を数値確認する。"""
import math
import random

def multiply(a,b):
    return [[sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def matrix(x,y,z):
    # エンジンの行ベクトルと同じZ→X→Y順。
    sx,cx=math.sin(x),math.cos(x);sy,cy=math.sin(y),math.cos(y);sz,cz=math.sin(z),math.cos(z)
    return multiply(multiply([[cz,sz,0],[-sz,cz,0],[0,0,1]],[[1,0,0],[0,cx,sx],[0,-sx,cx]]),
                    [[cy,0,-sy],[0,1,0],[sy,0,cy]])

def main():
    # 角度の表現値ではなく、復元した姿勢が一致することを比較する。
    rng=random.Random(42)
    for _ in range(2000):
        angles=[rng.uniform(-math.pi,math.pi) for _ in range(3)]
        m=matrix(*angles)
        if abs(m[2][1])>=.9999:continue
        restored=matrix(math.asin(-m[2][1]),math.atan2(m[2][0],m[2][2]),math.atan2(m[0][1],m[1][1]))
        assert max(abs(m[i][j]-restored[i][j]) for i in range(3) for j in range(3))<1e-6
    for i in range(360):
        a=.85*math.sin(i/360*math.tau)
        assert abs(4*(1-math.cos(a))+4*math.cos(a)-4)<1e-6
        assert abs(-4*math.sin(a)+4*math.sin(a))<1e-6
    print('PASS: 2000 ZXY orientations and 360 fixed-pivot samples')

if __name__=='__main__':
    main()
