# x軸
set xlabel "K" font "Times New Roman, 30" offset 0,-1
set logscale x
set xrange[1:1000]
set xtics (1,2,4,8,16,32,64,128,256,512,900)

# y軸
set ylabel "Time [ms]" font "Times New Roman, 30" offset -3,0
set logscale y
set format y "10^{%L}"
set yrange [0:100000]
set ytics(0,1,10,100,1000,10000,100000)

set tics font "Times New Roman, 25"

set lmargin -10

# 凡例
set key right bottom font "Times New Roman, 25"

# 読み込み
input = "'clusteringBasedBilateralFilterTest/save_rgb_time.dat'"
output = "'test.pdf'"
# permutohedral lattice
permutohedral(x)=50
# gaussian kd-tree
gauss(x)=40

set size 0.96, 1.0
set origin 0.04,0.0
plot gauss(x) title "GKDT", permutohedral(x) title "PL", @input using 1:2 w l title "1-ch.", @input using 3:4 w l title "2-ch.", @input using 5:6 w l title "3-ch."

set term pdf 
set output @output
replot
