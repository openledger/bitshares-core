BitShares find cycled accounts
==============

Contains helpers to find cycled by authority accounts

ES_URL - elasticserch url

To find potential cycled accounts (those who has active or owner account_auths not empty).

To get a count of matched accounts:
$ curl -X GET 'ES_URL/objects-account/_search?size=0&pretty=true' -H 'Content-Type: application/json' -d @elastic_find_auth.json

To dump into a file a search result (where NUMBER is a count from previous request):
$ curl -X GET 'ES_URL/objects-account/_search?size=NUMBER&pretty=true' -H 'Content-Type: application/json' -d @elastic_find_auth.json > result.json

To remove unused data:
$ ./delete_lines.sh result.json 

To find and dump cycled accounts into a file:
$ python ./find_cycled_accounts.py
