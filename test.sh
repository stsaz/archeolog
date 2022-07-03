set -x
set -e

if ! test -f LOG ; then
	echo '18:48:12.685 line1
18:48:12.685 line2
18:48:12.686 line3
18:48:12.687 line4
' >LOG
fi

./archeolog LOG

./archeolog LOG -s '18:48:12.684'
./archeolog LOG -s '18:48:12.685'
./archeolog LOG -s '18:48:12.686'
./archeolog LOG -s '18:48:12.687'
./archeolog LOG -s '18:48:12.688' || true

./archeolog LOG -e '18:48:12.684'
./archeolog LOG -e '18:48:12.685'
./archeolog LOG -e '18:48:12.686'
./archeolog LOG -e '18:48:12.687'
./archeolog LOG -e '18:48:12.688'

./archeolog LOG -s '18:48:12.685' -e '18:48:12.685'
./archeolog LOG -s '18:48:12.685' -e '18:48:12.686'
./archeolog LOG -s '18:48:12.685' -e '18:48:12.687'
