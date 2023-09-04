cnt=$1
echo " child tables count=$cnt"

for ((i=0; i<$cnt; i++ ))
do
    taos -Q "root.test.meters."  -s "select * from test.d$i >> /root/out/d$i.csv;"
    echo -e $GREEN loop $i $NC
done
