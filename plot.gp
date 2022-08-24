# x軸
set xlabel "K" font "Times New Roman, 30" offset 0,-1
set logscale x
set xrange[1:1000]
set xtics (1,2,4,8,16,32,64,128,256,512,900)

# y軸
set ylabel "PSNR [dB]" font "Times New Roman, 30" offset -3,0
set yrange [10:100]
#set logscale y
#set ylabel "Time [ms]" font "Times New Roman, 30" offset -3,0
#set format y "10^{%L}"
#set yrange [0:100000]
#set ytics(0,1,10,100,1000,10000,100000)

set tics font "Times New Roman, 25"

set lmargin -10

# 凡例
set key right bottom font "Times New Roman, 25"

# 読み込み
input = "'test_dat/save_rgb.dat'"
output = "'rgb_psnr_K.pdf'"
# permutohedral lattice
permutohedral1(x)=47.762120
permutohedral2(x)=50.545063
permutohedral3(x)=50.150343

# gaussian kd-tree
gauss1(x)=43.882041
gauss2(x)=45.203163
gauss3(x)=44.857090

set size 0.96, 1.0
set origin 0.04,0.0

# gauss2(x) title "GKDT2", \
# permutohedral2(x) title "PL2", \

plot gauss1(x) lw 2.0 lc "dark-cyan" title "GKDT1", \
    gauss3(x) lw 2.0 lc "blue" title "GKDT3", \
    permutohedral1(x) lc "green" lw 2.0 title "PL1", \
    permutohedral3(x) lc "dark-green" lw 2.0 title "PL3", \
    @input using 1:2 w l lc "magenta" lw 2.0  title "1-ch.", \
    @input using 3:4 w l lc "orange" lw 2.0  title "2-ch.", \
    @input using 5:6 w l lc "red" lw 2.0  title "3-ch."

set term pdf 
set output @output
replot
