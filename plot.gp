# x軸
set xlabel "K" font "Times New Roman, 30" offset 0,-1
set logscale x
set xrange[1:1000]
set xtics (1,2,4,8,16,32,64,128,256,512,900)

# y軸
set ylabel "PSNR [dB]" font "Times New Roman, 30" offset -3,0
set yrange [10:80]
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
input = "'test_dat/save_rgbd.dat'"
output = "'rgbd_psnr_K.pdf'"
# permutohedral lattice
permutohedral1(x)=47.576813
permutohedral2(x)=54.907369
permutohedral3(x)=56.526781

# gaussian kd-tree
gauss1(x)=47.866335
gauss2(x)=53.787291
gauss3(x)=54.013862

set size 0.96, 1.0
set origin 0.04,0.0
plot gauss1(x) title "GKDT1", \
    gauss2(x) title "GKDT2", \
    gauss3(x) title "GKDT3", \
    permutohedral1(x) title "PL1", \
    permutohedral2(x) title "PL2", \
    permutohedral3(x) title "PL3", \
    @input using 1:2 w l title "1-ch.", \
    @input using 3:4 w l title "2-ch.", \
    @input using 5:6 w l title "3-ch."

set term pdf 
set output @output
replot
