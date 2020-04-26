if [ $# -lt 1 ]
then
	echo "Usage: bash run_rp_pams.sh <number of mobile devices>"
	exit
fi
sudo pkill dmm_proxy
sleep 1
sudo rmmod -f ss
sudo insmod /home/shawnzhu/dmm-proj/MPBond/server/ss.ko
sudo /home/shawnzhu/dmm-proj/MPBond/server/dmm_proxy $1 15 1 1 1
sudo rmmod -f ss
echo "finish."