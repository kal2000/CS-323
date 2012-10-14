#/usr/bin/awk -f
function convert(str)
{
  split(str,Arr,":")
  return 60*Arr[1]+Arr[2]
}
{ print $2, sum+=convert($2) }
END {print sum}