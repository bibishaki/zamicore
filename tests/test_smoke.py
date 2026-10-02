import torch
from core.representations import ActData
from core.clustering import OnlineKMeans
from graph.transitions import Transitions
from topology.complex import Complex

def test_pipeline_components():
    # 1. Проверка контейнера ActData
    T, L, d = 10, 4, 16
    vu = torch.randn(T, 2, L, d)
    p = torch.randn(T, d)
    act = ActData(VU=vu, P=p)
    assert act.T == 10

    # 2. Проверка кластеризации и весов
    cfg = {"beta_K_0": 4.0, "b_0": 0.01, "r": 1.5}
    kmeans = OnlineKMeans(dim=d, config=cfg)
    for t in range(T):
        kmeans.update_centroids_step(p[t])
    w = kmeans.finalize_act(p)
    assert w.shape[0] == T

    # 3. Проверка переходов и триангуляции
    k_star = torch.tensor([0, 0, 1, 1, 2, 2, 0])
    transitions = Transitions()
    _, E, _, _ = transitions.compute(k_star)
    
    comp = Complex()
    k_data = comp.compute(E)
    assert k_data.total_size >= 0
