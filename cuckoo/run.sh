# A function to echo in blue color
function blue() {
	es=`tput setaf 4`
	ee=`tput sgr0`
	echo "${es}$1${ee}"
}

blue ""
blue "Running simple"
shm-rm.sh 1>/dev/null 2>/dev/null
sudo numactl --physcpubind=0-7 --interleave=0,1 ./simple
