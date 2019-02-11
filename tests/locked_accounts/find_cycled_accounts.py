import json
import networkx as nx

def auth_from_json(json_str):
    result = []
    json_obj = json.loads(json_str)

    for el in json_obj:
        result.append(el[0])
    return result


class Account(object):
    def __init__(self, json_obj):
        self.account_id = json_obj['object_id']
        self.owner_account_auths = auth_from_json(json_obj['owner_account_auths'])
        self.active_account_auths = auth_from_json(json_obj['active_account_auths'])
        self.total_in_cycle = []

    def to_json(self):
        return json.dumps(self, default=lambda o: o.__dict__, sort_keys=True, indent=4)


'''
create a dict{id, account} from json
'''
def collect_accounts(json_str):
    accounts = {}
    for el in json_str['hits']['hits']:
        account = Account(el['_source'])
        accounts[account.account_id] = account
    return accounts


'''
    Base filter class
'''
class Filter(object):
    def __init__(self, next_filter=None):
        self.next_filter = next_filter

    def filter(self, account):
        self.__do_filter__(account)

    def __do_filter__(self, account):
        if self.next_filter : 
            self.next_filter.filter(account)


'''
    CycledAccountFilter filters exactly cycled accounts
    and put them into accum
    def Exactly cycled account - is an account that has own id in own auth(active or onwner)
'''
class CycledAccountFilter(Filter):
    def __init__(self, accum, next_filter=None):
        super(CycledAccountFilter, self).__init__(next_filter)
        self.cycled_accounts = accum

    def filter(self, account):
        authorities = account.active_account_auths + account.owner_account_auths
        if account.account_id in authorities:
            self.cycled_accounts[account.account_id] = account
        else:
            super(CycledAccountFilter, self).filter(account)


'''
    PotentialCycledAccountFilter filters potentially cycled accounts, only those who has
    in own auth(active or owner) an account_id that is in a storage and puts them into accum,
    otherwithe - skip such an account
    Storage contains potential cycled accounts
'''
class PotentialCycledAccountFilter(Filter):
    def __init__(self, accum, storage, next_filter=None):
        super(PotentialCycledAccountFilter, self).__init__(next_filter)
        self.potential_cycled_accounts = accum
        self.storage = storage

    def filter(self, account):
        authorities = account.active_account_auths + account.owner_account_auths
        for account_id in authorities:
            if account_id in self.storage:
                self.potential_cycled_accounts[account.account_id] = account
                break


class CycleFinder(object):
    def __init__(self, accounts):
        self.graph = nx.MultiDiGraph()
        for account in accounts:
            self.graph.add_node(account.account_id)
            auth = account.active_account_auths + account.owner_account_auths
            for a in auth:
                self.graph.add_node(a)
                self.graph.add_edge(account.account_id, a)

    def find_cycles(self, accounts):
        cycled = []
        for account in accounts:
            try:
                account.total_in_cycle = list(nx.find_cycle(self.graph, account.account_id))
                cycled.append(account)
            except:
                pass
        return cycled


def dump_cycled_accounts(file_name, accounts):
    with open(file_name, 'w') as f:
        for account in accounts:
            f.write(account.to_json())


def load_accounts(file_name):
    accounts = {}
    with open(file_name) as f:
        data = json.load(f)
        accounts = collect_accounts(data)
    return accounts


def main():
    accounts = load_accounts('result.json')
    potential_cycled_accounts = {}
    potential_cycled_filter = PotentialCycledAccountFilter(potential_cycled_accounts, accounts)

    self_cycled_accounts = {}
    cycled_filter = CycledAccountFilter(self_cycled_accounts, potential_cycled_filter)

    map(cycled_filter.filter, accounts.values())

    print 'cycled account count is: {}'.format(len(self_cycled_accounts))
    print 'potential cycled account count is: {}'.format(len(potential_cycled_accounts))

    finder = CycleFinder(self_cycled_accounts.values() + potential_cycled_accounts.values())
    cycled = finder.find_cycles(self_cycled_accounts.values() + potential_cycled_accounts.values())

    print 'total_cycles: {}'.format(len(cycled))
    dump_cycled_accounts('cycled_accounts.json', cycled)

if __name__ == '__main__':
    main()