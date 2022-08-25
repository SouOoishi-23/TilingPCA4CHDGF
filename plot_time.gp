# x軸
set xlabel "K" font "Times New Roman, 30" offset 0,-1
set logscale x
set xrange[1:1000]
set xtics (1,2,4,8,16,32,64,128,256,512,900)

# y軸
#set ylabel "PSNR [dB]" font "Times New Roman, 30" offset -3,0
#set yrange [10:90]
set logscale y
set ylabel "Time [ms]" font "Times New Roman, 30" offset -3,0
set format y "10^{%L}"
set yrange [0:100000]
set ytics(0,1,10,100,1000,10000,100000)

set tics font "Times New Roman, 25"

set lmargin -10

# 凡例
set key right bottom font "Times New Roman, 25"

# 読み込み
input = "'test_dat/save_nlm_time.dat'"
output = "'nlm_time_K.pdf'"

# Time  permutohedral lattice & gkdt
#RGB
permutohedral1(x)=35.418700
permutohedral3(x)=19.859500 
gauss1(x)=137.476200
gauss3(x)=130.964800 

#RGBIR
#permutohedral1(x)=55.668600
#permutohedral3(x)=41.543200
#gauss1(x)=116.084600
#gauss3(x)=114.623100

#RGBD
#permutohedral1(x)=44.755700
#permutohedral3(x)=30.636600
#gauss1(x)=124.39170
#gauss3(x)=126.831600

#FnF
#permutohedral1(x)=28.265000
#permutohedral3(x)=80.750000
#gauss1(x)=120.282100
#gauss3(x)=132.679500

#NLM
permutohedral1(x)=50.848900
permutohedral3(x)=4285.471000
gauss1(x)=129.975300
gauss3(x)=215.203800 

#HSI
#permutohedral1(x)=219.029800
#permutohedral3(x)=7782.585100
#gauss1(x)=210.895500
#gauss3(x)=252.751500

set size 0.96, 1.0
set origin 0.04,0.0

# gauss2(x) title "GKDT2", \
# permutohedral2(x) title "PL2", \

plot gauss1(x) lw 2.0 lc "dark-cyan" notitle, \
    gauss3(x) lw 2.0 lc "blue" notitle, \
    permutohedral1(x) lc "green" lw 2.0 notitle, \
    permutohedral3(x) lc "dark-green" lw 2.0 notitle, \
    @input using 1:2 w l lc "magenta" lw 2.0  notitle, \
    @input using 3:4 w l lc "orange" lw 2.0  notitle "2-ch.", \
    @input using 5:6 w l lc "red" lw 2.0  notitle

set term pdf 
set output @output
replot
